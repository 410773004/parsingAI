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
#include "assert.h"
#include "stdlib.h"
#include "rainier_soc.h"
#ifdef MPC
#include "sect.h"
#include "bf_mgr.h"
#include "queue.h"
#include "ncl.h"
#include "spin_lock.h"
#include "idx_meta.h"
#include "ddr.h"

#define __FILEID__ idx
#include "trace.h"
/*! \file idx_meta.h
 * @brief define index meta initialization and allocation
 *
 * \addtogroup utils
 * \defgroup idx_meta
 * \ingroup idx_meta
 * @{
 */

share_data_zi struct nand_info_t shr_nand_info;		///< nand info data structure
share_data_zi void *shr_idx_meta[MAX_IDX_META];		///< index meta buffer pointer
share_data_zi volatile u32 shr_idx_meta_allocated[MAX_IDX_META];	///< index meta was already allocated

// save memory here
#define DIE_Q_MRG_META_CNT		(PAD_META_TOTAL * 48) //32 for b17 max
#define SRAM_IDX_META_CNT		(256 + DIE_Q_MRG_META_CNT)
#define DDR_IDX_META_CNT		(1024)

#if SRAM_IDX_META_CNT > 1024
#error "SRAM index overflow"
#endif

#if DDR_IDX_META_CNT > 1024
#error "DDR index overflow"
#endif

init_code void idx_meta_init(void)
{
	u32 ttl_meta_sz = sizeof(struct du_meta_fmt) * SRAM_IDX_META_CNT;

	sys_assert(shr_idx_meta[SRAM_IDX_META] == NULL);
	shr_idx_meta[SRAM_IDX_META] = sys_malloc_aligned(SLOW_DATA, ttl_meta_sz, sizeof(struct du_meta_fmt));
	sys_assert(shr_idx_meta[SRAM_IDX_META]);

	ttl_meta_sz = sizeof(struct du_meta_fmt) * DDR_IDX_META_CNT;
	u32 ddr_meta_start = ddr_dtag_register(occupied_by(ttl_meta_sz, DTAG_SZE));
	sys_assert(ddr_meta_start != ~0);
	shr_idx_meta[DDR_IDX_META] = (void*)ddtag2mem(ddr_meta_start);

	shr_idx_meta_allocated[SRAM_IDX_META] =
	shr_idx_meta_allocated[DDR_IDX_META] = 0;

	ncl_set_meta_base(shr_idx_meta[SRAM_IDX_META], META_IDX_SRAM_BASE);
	ncl_set_meta_base(shr_idx_meta[DDR_IDX_META], META_IDX_DDR_BASE);
}

init_code void *idx_meta_allocate(u32 req, u32 type, u32 *idx)
{
	void *ret;
	u32 max_cnt[2] = {SRAM_IDX_META_CNT, DDR_IDX_META_CNT};

	sys_assert(type < MAX_IDX_META);

	spin_lock_take(SPIN_LOCK_KEY_IDX_META, 0, true);

	utils_apl_trace(LOG_ERR, 0x6bcb, "%d meta alloc:[%d/%d] add %d -> %d\n",type,shr_idx_meta_allocated[type],max_cnt[type],req,shr_idx_meta_allocated[type] + req);
	if (shr_idx_meta_allocated[type] + req > max_cnt[type])
		panic("not enough");

	ret = ptr_inc(shr_idx_meta[type], sizeof(struct du_meta_fmt) * shr_idx_meta_allocated[type]);
	*idx = shr_idx_meta_allocated[type];
	shr_idx_meta_allocated[type] += req;

	spin_lock_release(SPIN_LOCK_KEY_IDX_META);

	return ret;
}

#if (SPOR_FLOW == mENABLE)
// added by Sunny 20200828
init_code u32 idx_meta_get_shr_alloc_cnt(u32 type)
{
	return shr_idx_meta_allocated[type];
}
#endif
#endif
/*! @} */
