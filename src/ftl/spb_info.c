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
/*! \file spb_info.c
 * @brief spb info is a table to store erase count, and pb type
 *
 * \addtogroup spb_info
 * @{
 *
 * spb info is a global table and it is stored in SPB log, the reconstruction
 * method of spb log is exported from FTL, outside module can reconstructed it
 * to get spb info table without FTL initialization.
 */
#include "ftlprecomp.h"
#include "queue.h"
#include "ncl_exports.h"
#include "spb_info.h"
#include "ftl_flash_geo.h"
#include "spb_log.h"
#include "spb_log_flush.h"
#include "console.h"
#include "defect_mgr.h"
#include "ftl.h"
#include "ipc_api.h"
#include "spb_mgr.h"

/*! \cond PRIVATE */
#define __FILEID__ spbinfo
#include "trace.h"
/*! \endcond */

/*!
 * @brief dump function for spb info
 *
 * @param spb_id	SPB id to dump information
 *
 * @return		none
 */
static void spb_info_dump(u32 spb_id);

/*!
 * @brief (DEBUG)uart interface for dump spb info
 *
 * @param argc	number of argument
 * @param argv	argument list. First argument is spb_id. If no spb_id, dump all.
 *
 * @return	return -1 for wrong input arguments, or 0 for successfully
 */
static int spb_info_dump_main(int argc, char *argv[]);

fast_data_zi spb_info_t *spb_info_tbl;	///< spb info table

#if (SPOR_VAC_EC_IN_PLP_EPM == mENABLE)
extern u32 shr_spb_info_ddtag;
#endif

ddr_code ftl_err_t spb_info_init(ftl_initial_mode_t mode)
{
	u32 spb_cnt = get_total_spb_cnt();
	u32 size = spb_cnt * sizeof(spb_info_t);
	log_level_t level = ipc_api_log_level_chg(LOG_INFO);
	ftl_err_t ret = FTL_ERR_OK;

	if(mode == FTL_INITIAL_PREFORMAT)
	{
		u32 blk_idx;
		for(blk_idx=0; blk_idx<spb_cnt; blk_idx++)
		{
			spb_info_tbl[blk_idx].block = 0;
			spb_info_tbl[blk_idx].flags = 0;
//			spb_info_tbl[blk_idx].poh   = 0;    //no need to init poh
			spb_info_tbl[blk_idx].pool_id = 0;
		}
		spb_info_tbl[SRB_BLK_SYSINFO_IDX].pool_id = SPB_POOL_MAX;
		ftl_spblog_trace(LOG_ALW, 0xe655, "[FTL] preformat spb_info_init");
		ftl_ns_check_appl_allocating();
	}
	else
	{
		spb_info_tbl = (spb_info_t*) ftl_get_io_buf(size);
		memset((void*) spb_info_tbl, 0, spb_cnt * sizeof(spb_info_t));
#if (SPOR_VAC_EC_IN_PLP_EPM == mENABLE)
        shr_spb_info_ddtag = mem2ddtag(spb_info_tbl);
        ftl_spblog_trace(LOG_ALW, 0xe83e, " spb_info_tbl : 0x%x, shr_spb_info_ddtag : 0x%x",\
                         spb_info_tbl, shr_spb_info_ddtag);
#endif

		spb_info_tbl[SRB_BLK_SYSINFO_IDX].pool_id = SPB_POOL_MAX;
		ftl_spblog_trace(LOG_ALW, 0x1a48, "[FTL] poweron spb_info_init");
	}
	ipc_api_log_level_chg(level);
	return ret;
}

init_code ftl_err_t spb_info_flag_reset(void)
{
	u32 spb_cnt = get_total_spb_cnt();
	u32 i;

	for (i = 0; i < spb_cnt; i++) {
		spb_info_tbl[i].flags = 0;
		spb_info_tbl[i].pool_id = 0;
	}

	return FTL_ERR_VIRGIN;
}

fast_code void spb_info_flush(spb_id_t spb_id)
{
	u32 page_id;

	if (spb_id == INV_SPB_ID) {
		page_id = ~0;
	} else {
		u32 offset = spb_id * sizeof(spb_info_t);

		page_id = offset >> (DU_CNT_SHIFT + ctz(NAND_DU_SIZE));
	}

	spb_log_flush(page_id);
}

fast_code __attribute__((__unused__)) void spb_info_dump(u32 spb_id)
{
	u32 i;
	u32 start;
	u32 end;
	u32 avg[4] = {0, 0, 0, 0};
	u32 sum[4] = {0, 0, 0, 0};
	u32 mx[4] = {0, 0, 0, 0};
	u32 mn[4] = {~0, ~0, ~0, ~0};
	u32 cnt[4] = {0, 0, 0, 0};

	start = (spb_id == INV_SPB_ID) ? 0 : spb_id;
	end = (spb_id == INV_SPB_ID) ? get_total_spb_cnt() : (spb_id + 1);

	for (i = start; i < end; i++) {
		ftl_defect_t ftl_df;
		spb_desc_t* desc;

		ftl_df.df[0] = 0;
		ftl_df.df[1] = 0;

		spb_get_defect_map(i, &ftl_df);
		ftl_apl_trace(LOG_ALW, 0xa850, "spb %d pec %d flags 0x%x pool %d def 0x%x %x", i,
			spb_info_tbl[i].erase_cnt,
			spb_info_tbl[i].flags,
			spb_info_tbl[i].pool_id,
			ftl_df.df[0], ftl_df.df[1]);

		desc = spb_get_desc(i);
		ftl_apl_trace(LOG_ALW, 0x41a3, "spb %d rd_cnt %d sn %d flags 0x%x sw flags 0x%x",
			i, desc->rd_cnt, desc->sn, desc->flags, spb_get_sw_flag(i));

		sum[spb_info_tbl[i].pool_id] += spb_info_tbl[i].erase_cnt;
		if (mx[spb_info_tbl[i].pool_id] < spb_info_tbl[i].erase_cnt)
			mx[spb_info_tbl[i].pool_id] = spb_info_tbl[i].erase_cnt;

		if (mn[spb_info_tbl[i].pool_id] > spb_info_tbl[i].erase_cnt)
			mn[spb_info_tbl[i].pool_id] = spb_info_tbl[i].erase_cnt;

		cnt[spb_info_tbl[i].pool_id]++;
	}

	for (i = 0; i < 4; i++) {
		if (cnt[i] != 0) {
			avg[i] = sum[i] / cnt[i];
			ftl_apl_trace(LOG_ALW, 0x8223, "Pool %d avg %d max %d min %d",
					i, avg[i], mx[i], mn[i]);
		}
	}
}

static ddr_code int spb_info_dump_main(int argc, char *argv[])
{
	if (argc == 1) {
		spb_info_dump(0xFFFF);
	}else {
		u32 spb_id = atoi(argv[1]);
		u32 spb_cnt = get_total_spb_cnt();

		if (spb_id >= spb_cnt) {
			ftl_apl_trace(LOG_INFO, 0x5961, "Invalid SPB id, range [0-%d]",
					(spb_cnt - 1));
			return -1;
		}

		spb_info_dump(spb_id);
	}

	return 0;
}

/*! \cond PRIVATE */
static DEFINE_UART_CMD(spb_info_dump, "spb_info_dump", "spb_info_dump [spb_id]",
		"spb_info_dump: help function "
		"to dump SPB information, if no spb_id input, dump all.",
		0, 1, spb_info_dump_main);
/*! \endcond */
/*! @} */
