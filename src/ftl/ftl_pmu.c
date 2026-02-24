#include "ftlprecomp.h"
#include "ftl_export.h"
#include "blk_pool.h"

/*! \cond PRIVATE */
#define __FILEID__ fpmu
#include "trace.h"
/*! \endcond */

share_data volatile pblk_t fc_pmu_pblk;
share_data volatile u16 cur_page;

fast_code bool pmu_swap_file_order(u32 cnt)
{
	u32 r = shr_nand_info.geo.nr_pages / XLC - cur_page;

	if (cnt > r) {
		blk_pool_put_pblk(fc_pmu_pblk);
		//blk_pool_get_pblk(&fc_pmu_pblk); //Curry
		cur_page = 0;
	}
	return true;
}

init_code void pmu_swap_pblk_init(void)
{
	//blk_pool_get_pblk(&fc_pmu_pblk); //Curry
	cur_page = 0;
}

fast_code void pmu_swap_end(void)
{
	blk_pool_put_pblk(fc_pmu_pblk);
	cur_page = 0;
}

