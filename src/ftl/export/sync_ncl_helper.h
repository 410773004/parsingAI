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
#pragma once

#include "types.h"
#include "ftltype.h"
#include "assert.h"
#include "stdlib.h"
#include "string.h"
#include "bf_mgr.h"
#include "queue.h"
#include "dma.h"
#include "ncl_exports.h"

typedef struct _ncl_page_res_t {
	struct ncl_cmd_t ncl_cmd;
	pda_t pda[DU_CNT_PER_PAGE];
	bm_pl_t bm_pl[DU_CNT_PER_PAGE];
	struct info_param_t info[DU_CNT_PER_PAGE];
} ncl_page_res_t;

typedef struct _ncl_blk_res_t {
	struct ncl_cmd_t ncl_cmd;
	pda_t pda;
	struct info_param_t info;
} ncl_blk_res_t;

/*!
 * @brief erase a spb synchronously
 *
 * @param spb_id	spb to be erase
 * @param type		erase type
 * @param info_list	info list for status updating
 *
 * @return		return true if erase successfully
 */
bool ncl_spb_sync_erase(spb_id_t spb_id, nal_pb_type_t type,
		struct info_param_t** info_list);

/*!
 * @brief read du list synchronously
 *
 * @param pda_list	pda list to be read
 * @param cnt		list length
 * @param bm_pl_list	bm payload list
 * @param info_list	info list for status updating
 * @param type		pb type. Doesn't support read XLC/SLC mixed
 * @param remap		if pda need to be remapped
 * @param host		if read for host dus
 *
 * @return		return NCL command status
 */
u16 ncl_read_dus(pda_t *pda_list, u32 cnt, bm_pl_t *bm_pl_list,
		struct info_param_t *info_list, nal_pb_type_t type, bool remap, bool host);

/*!
 * @brief read single du synchronously
 *
 * @param pda		pda to be read
 * @param bm_pl		bm payload
 * @param type		pb type
 * @param info		ncl info
 * @param remap		if pda need to be remap
 *
 * @return		return NCL command status
 */
u16 ncl_read_du(pda_t pda, bm_pl_t bm_pl, nal_pb_type_t type,
		struct info_param_t *info, bool remap);

/*!
 * @brief read single page synchronously
 *
 * @param pda		pda to be read, must be page pda
 * @param ptr		data buffer
 * @param meta_idx	meta buffer index
 *
 * @return		merged du status of whole page
 */
nal_status_t ncl_read_one_page(pda_t pda, void *ptr, u32 meta_idx);

/*!
 * @brief write pages synchronously
 *
 * @param pda_list	pda list to be programmed
 * @param info_list	ncl info list
 * @param list_len	list length
 * @param bm_pl		bm payload list
 * @param flags		ncl command flags
 *
 * @return		return true for programmed successfully
 */
bool ncl_write_pages(pda_t *pda_list, struct info_param_t *info_list,
		u32 list_len, bm_pl_t *bm_pl, u32 flags);

/*!
 * @brief api to remap reinit
 *
 * @param	not used
 *
 * @return	not used
 */
void ncl_sync_reinit_remap(void);

/*!
 * @brief change local CPU and CPU BE log level
 *
 * @param new_lvl	new log level
 *
 * @return		old log level
 */
u32 ftl_log_level_chg(u32 new_lvl);

/*! @} */
