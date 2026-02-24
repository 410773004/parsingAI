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

/*! \file tune_vref.c
 * @brief get nand VREF by histogram(old)
 *
 * \addtogroup test
 * \defgroup tune_vref
 * \ingroup test
 *
 * @{
 */

#include "ndcu.h"
#include "nand.h"
#include "eccu.h"
#include "ficu.h"
#include "ncl.h"
#include "nand_cfg.h"
#include "ncl_cmd.h"
#include "eccu_reg_access.h"
#include "ncb_eccu_register.h"

/*! \cond PRIVATE */
#define __FILEID__ hynix_tune
#include "trace.h"
/*! \endcond */

extern void ficu_mode_ard_control(u32 mode);

slow_code void set_param(u8 ch, u8 ce, u8 fa, u8 val)
{
	u32 value = val;
	ndcu_ind_t ctrl = {
		.write = true,
		.cmd_num = 1,
		.reg1.b.ind_byte0 = 0x36,
		.reg1.b.ind_byte1 = fa,
		.xfcnt = 1,
		.buf = (u8 *)&value,
	};

	ndcu_open(&ctrl, ch, ce);
	ndcu_start(&ctrl);

	do {
		if (ndcu_xfer(&ctrl))
			break;
	} while (1);
	ndcu_close(&ctrl);

	ctrl.cmd_num = 0;
	ctrl.reg1.b.ind_byte0 = 0x16;
	ctrl.xfcnt = 0;
	ndcu_open(&ctrl, ch, ce);
	ndcu_start(&ctrl);
	ndcu_close(&ctrl);
}


slow_code u8 get_param(u8 ch, u8 ce, u8 fa)
{
	u32 data;
	ndcu_ind_t ctrl = {
		.write = false,
		.cmd_num = 1,
		.reg1.b.ind_byte0 = 0x37,
		.reg1.b.ind_byte1 = fa,
		.xfcnt = 1,
		.buf = (u8 *)&data,
	};

	ndcu_open(&ctrl, ch, ce);
	ndcu_start(&ctrl);

	do {
		if (ndcu_xfer(&ctrl))
			break;
	} while (1);
	ndcu_close(&ctrl);
	//ncl_test_trace(LOG_ERR, 0, "%b %x\n", fa, data);

	//set_param(ch, ce, fa, data+2);
	return (u8)data;
}

#if XLC == 3
#define XLC_VREF_NUM	7
#elif XLC == 4
#define XLC_VREF_NUM	15
#endif

#if HAVE_HYNIX_SUPPORT
#include "nand_sk.h"
u8 get_vref_max(u32 vref_idx)
{
	if (vref_idx < 4) {
		return 0xFF;
	} else {
		return 0xE7;
	}
}

u8 get_vref_min(u32 vref_idx)
{
	if (vref_idx < 4) {
		return 0x19;
	} else {
		return 0x00;
	}
}

void set_nand_vref(u32 vref_idx, u32 vref_value)
{
	ficu_mode_disable();
	ndcu_en_reg_control_mode();
	set_param(0, 0, 0xF + vref_idx, vref_value);
	ndcu_dis_reg_control_mode();
	ficu_mode_enable();
}
u32 vref_default[XLC_VREF_NUM] = {0x1D, 0x4F, 0x74, 0x9C, 0x37, 0x60, 0x8B};
u32 vref_page_type[XLC_VREF_NUM] = {2, 1, 0, 1, 2, 1, 0};
u32 vref_addr[XLC_VREF_NUM] = {0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15};
#endif

// Tune vref by histogram, intend to use this method for all nand vendors
void uniform_tune_vref(pda_t pda)
{
	u32 vref_best[XLC_VREF_NUM] = {0};
	u32 vref_idx;
	u32 page;
	u32 page_type = pda >> nand_info.pda_page_shift;

	page = (pda >> nand_info.pda_page_shift) & nand_info.pda_page_mask;
	page_type = get_xlc_page_type(page);
	switch(page_type) {
	case XLC_LOW_PAGE:
		ncl_test_trace(LOG_ERR, 0x8b81, "Low page\n");
		break;
	case XLC_MID_PAGE:
		ncl_test_trace(LOG_ERR, 0xce3b, "Mid page\n");
		break;
	case XLC_UPR_PAGE:
		ncl_test_trace(LOG_ERR, 0x2b21, "Upr page\n");
		break;
	}
	log_level_chg(LOG_ALW);
	ficu_mode_ard_control(0);
	eccu_set_dec_mode(DEC_MODE_MDEC);

	for (vref_idx = 0; vref_idx < XLC_VREF_NUM; vref_idx++) {
		set_nand_vref(vref_idx, vref_default[vref_idx]);
	}

	// Check basically 0/1 bits even at default vref
	extern u32 pda_1bits_cnt(pda_t pda, nal_pb_type_t pb_type);
	u32 cnt1, cnt0;
	u32 cnt_half;
	cnt1 = pda_1bits_cnt(pda, 1);
	cnt0 = get_encoded_du_size() * 8 - cnt1;
	cnt_half = get_encoded_du_size() * 4;
	if (cnt0 < (cnt_half - cnt_half / 4)) {
		sys_assert(0);
	}
	if (cnt1 < (cnt_half - cnt_half / 4)) {
		sys_assert(0);
	}

	u16* cnt_array;
	cnt_array = sys_malloc(SLOW_DATA, 256*sizeof(u16));
	u32 j;
	u32 vref_min;
	u32 vref_max;

	for (vref_idx = 0; vref_idx < XLC_VREF_NUM; vref_idx++) {
		if (vref_page_type[vref_idx] != page_type) {
			// Skip irrelavant vrefs
			continue;
		}

		vref_min = get_vref_min(vref_idx);
		vref_max = get_vref_max(vref_idx);

		// Count 1 bits count at different vrefs
		for (j = vref_min; j <= vref_max; j++) {
			set_nand_vref(vref_idx, j);
			cnt1 = pda_1bits_cnt(pda, 1);
			cnt_array[j] = cnt1;
		}

		// Make sure array incremental
		if (cnt_array[vref_max] < cnt_array[vref_min]) {
			for (j = vref_min; j <= vref_max; j++) {
				cnt_array[j] = get_encoded_du_size() * 8 - cnt_array[j];
			}
		}

		// Convert 1 bits count to histogram
		for (j = vref_min; j <= vref_max; j++) {
			if (cnt_array[j + 1] < cnt_array[j]) {
				cnt_array[j + 1] = cnt_array[j];
				if (j > 0) {
					cnt_array[j] = cnt_array[j - 1];
				} else {
					cnt_array[j] = 0;
				}
			} else {
				cnt_array[j] = cnt_array[j + 1] - cnt_array[j];
			}
			//ncl_test_trace(LOG_ERR, 0, "j 0x%b %d\n", j, cnt_array[j]);
		}

		bool range = false;
		u32 start = 0;
		u32 end = 0;
		u32 min = ~0;

		// Find rough range
		for (j = vref_min; j <= vref_max; j++) {
			if (cnt_array[j] > 50) {
				if (range) {
					end = j - 1;
					//ncl_test_trace(LOG_ERR, 0, "end 0x%b\n", end);
					if ((start <= vref_default[vref_idx]) && (vref_default[vref_idx] <= end)) {
						//ncl_test_trace(LOG_ERR, 0, "range 0x%b 0x%b\n", start, end);
						break;
					} else {
						range = false;
					}
				}
				continue;
			}

			if (!range) {
				range = 1;
				start = j;
				min = ~0;
				//ncl_test_trace(LOG_ERR, 0, "start 0x%b\n", start);
			}
			if (cnt_array[j] < min) {
				min = cnt_array[j];
			}
		}

		// Narrow down to smaller precise range
		range = false;
		u32 range_len = 0;
		u32 range_start = 0;
		u32 range_end = 0;
		u32 tmp_start;
		u32 tmp_end;
		for (j = start; j <= end; j++) {
			//ncl_test_trace(LOG_ERR, 0, "j 0x%b %d\n", j, cnt_array[j]);
			if (cnt_array[j] > min + 5) {
				if (range) {
					tmp_end = j - 1;
					u32 len;
					len = tmp_end - tmp_start + 1;
					//ncl_test_trace(LOG_ERR, 0, "[0x%b 0x%b]\n", tmp_start, tmp_end);
					if (len > range_len) {
						range_len = len;
						range_start = tmp_start;
						range_end = tmp_end;
					}
					range = false;
				}
				continue;
			}

			if (!range) {
				range = true;
				tmp_start = j;
				min = ~0;
			}
			if (cnt_array[j] < min) {
				min = cnt_array[j];
			}
		}
		sys_assert(range_len > 0);
		vref_best[vref_idx] = (range_start + range_end) / 2;
		ncl_test_trace(LOG_ERR, 0x7abe, "Final addr 0x%b, value 0x%b\n", vref_addr[vref_idx], vref_best[vref_idx]);
		set_nand_vref(vref_idx, vref_best[vref_idx]);
	}

	sys_free(SLOW_DATA, cnt_array);
}

/*! @} */
