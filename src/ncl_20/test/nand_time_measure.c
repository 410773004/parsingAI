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
/*! \file nand_time_measure.c
 * @brief test nand read/program/erase command time
 *
 * \addtogroup test
 * \defgroup nand_time_measure
 * \ingroup test
 *
 * @{
 */
#define __FILEID__ nclt
#include "trace.h"

void tsb_qlc_nand_measure(void)
{
	extern bool erase_pda(pda_t pda, nal_pb_type_t pb_type);
	extern u32 _ncl_get_tERS(pda_t pda, u32 pb_type);
	u32 time;
	u32 wl;
	u32 pg;
	u32 time_total = 0, time_min = ~0, time_max = 0;

	time = _ncl_get_tERS(0, 0);
	ncl_test_trace(LOG_ERR, 0x9778, "erase time %d %d\n", time, time/(SYS_CLK / 1000000));
	extern u32 _ncl_get_tPROG(pda_t pda, u32 pb_type, u32 step);
	time_total = 0;
	time_min = ~0;
	time_max = 0;
	for (pg = 0; pg < nand_page_num_slc(); pg++) {
		time = _ncl_get_tPROG(0 + (pg << nand_info.pda_page_shift), 0, 0);
		//ncl_test_trace(LOG_ERR, 0, "prog %d time %d %d\n", pg, time, time/(SYS_CLK / 1000000));
		time_total += time;
		if (time_max < time) {
			time_max = time;
		}
		if (time_min > time) {
			time_min = time;
		}
	}
	ncl_test_trace(LOG_ERR, 0xc416, "total %d, max %d, min %d\n", time_total / (SYS_CLK / 1000000) / nand_page_num_slc(), \
						time_max / (SYS_CLK / 1000000), \
						time_min / (SYS_CLK / 1000000));
	extern u32 _ncl_get_tR(pda_t pda, u32 pb_type);
	time_total = 0;
	time_min = ~0;
	time_max = 0;
	for (pg = 0; pg < nand_page_num_slc(); pg++) {
		time = _ncl_get_tR(0 + (pg << nand_info.pda_page_shift), 0);
		//ncl_test_trace(LOG_ERR, 0, "read %d time %d %d\n", pg, time, time/(SYS_CLK / 1000000));
		time_total += time;
		if (time_max < time) {
			time_max = time;
		}
		if (time_min > time) {
			time_min = time;
		}
	}
	ncl_test_trace(LOG_ERR, 0x56a3, "total %d, max %d, min %d\n", time_total / (SYS_CLK / 1000000) / nand_page_num_slc(), \
						time_max / (SYS_CLK / 1000000), \
						time_min / (SYS_CLK / 1000000));

	time = _ncl_get_tERS(0, 1);
	ncl_test_trace(LOG_ERR, 0xc1bc, "erase time %d %d\n", time, time/(SYS_CLK / 1000000));
	extern u32 _ncl_get_tPROG(pda_t pda, u32 pb_type, u32 step);
	for (wl = 0; wl < nand_page_num_slc(); wl++) {
		for (pg = 0; pg < XLC; pg++) {
			time = _ncl_get_tPROG(0 + (((wl * XLC) + pg) << nand_info.pda_page_shift), 1, PROG_1ST_STEP);
			//ncl_test_trace(LOG_ERR, 0, "prog %d time %d %d\n", (wl * XLC) + pg, time, time/(SYS_CLK / 1000000));
			time_total += time;
			if (time_max < time) {
				time_max = time;
			}
			if (time_min > time) {
				time_min = time;
			}
		}
		if (wl >= 4) {
			for (pg = 0; pg < XLC; pg++) {
				time = _ncl_get_tPROG(0 + ((((wl - 4) * XLC) + pg) << nand_info.pda_page_shift), 1, PROG_2ND_STEP);
				//ncl_test_trace(LOG_ERR, 0, "prog2%d time %d %d\n", ((wl - 4) * XLC) + pg, time, time/(SYS_CLK / 1000000));
				time_total += time;
				if (time_max < time) {
					time_max = time;
				}
				if (time_min > time) {
					time_min = time;
				}
			}
		}
	}
	for (wl = nand_page_num_slc() - 4; wl < nand_page_num_slc(); wl++) {
		for (pg = 0; pg < XLC; pg++) {
			time = _ncl_get_tPROG(0 + (((wl * XLC) + pg) << nand_info.pda_page_shift), 1, PROG_2ND_STEP);
			//ncl_test_trace(LOG_ERR, 0, "prog2%d time %d %d\n", (wl * XLC) + pg, time, time/(SYS_CLK / 1000000));
			time_total += time;
			if (time_max < time) {
				time_max = time;
			}
			if (time_min > time) {
				time_min = time;
			}
		}
	}
	ncl_test_trace(LOG_ERR, 0x738e, "total %d, max %d, min %d\n", time_total / (SYS_CLK / 1000000) / nand_page_num(), \
						time_max / (SYS_CLK / 1000000), \
						time_min / (SYS_CLK / 1000000));

	extern u32 _ncl_get_tR(pda_t pda, u32 pb_type);
	time_total = 0;
	time_min = ~0;
	time_max = 0;
	for (pg = 0; pg < nand_page_num(); pg++) {
		time = _ncl_get_tR(0 + (pg << nand_info.pda_page_shift), 1);
		//ncl_test_trace(LOG_ERR, 0, "read %d time %d %d\n", pg, time, time/(SYS_CLK / 1000000));
		time_total += time;
			if (time_max < time) {
				time_max = time;
			}
			if (time_min > time) {
				time_min = time;
			}
	}
	ncl_test_trace(LOG_ERR, 0x5d63, "total %d, max %d, min %d\n", time_total / (SYS_CLK / 1000000) / nand_page_num(), \
						time_max / (SYS_CLK / 1000000), \
						time_min / (SYS_CLK / 1000000));
}

/*! @} */
