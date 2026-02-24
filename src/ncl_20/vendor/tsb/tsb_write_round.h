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

/*!
 * @file tsb_write_round.h
 *
 * @date 1 Feb 2017
 * @brief TSB program order export API
 *
 */

#if !defined(_TSB_WRITE_ROUND_H_) && USE_TSB_NAND
#define _TSB_WRITE_ROUND_H_

/*!
 * @brief Get page count from round
 *
 * @param[in] round		program round
 *
 * @return page count
 */
static inline u32 toshiba_get_shared_page_cnt(u32 round)
{
	return XLC;
}

/*!
 * @brief Get round start page number
 *
 * @param[in] round		program round
 * @param[out] prog_step	program step
 *
 * @return round start page number
 */
static inline u32 toshiba_get_xlc_page(u32 round, u32 *prog_step)
{
#if XLC == 3
	*prog_step = 1;
	return round * 3;
#else
	static short order[2];
	static u32 cur_idx;
	u32 ret;

	if (round == 0) {
		order[0] = 0;
		order[1] = -1;
		cur_idx = 0;
	}

	*prog_step = cur_idx;
	ret = order[cur_idx];

	order[cur_idx]++;
	if ((order[cur_idx] & 3) == 0) {
		cur_idx = !cur_idx;
		if (order[cur_idx] == -1) {
			order[cur_idx] = 0;
			cur_idx = !cur_idx;
		} else if (order[cur_idx] >= 256) {
			cur_idx = !cur_idx;
		}
	}

	return ret << 2;	// return page
#endif
}

#endif /* end _TSB_WRITE_ROUND_H_ */
