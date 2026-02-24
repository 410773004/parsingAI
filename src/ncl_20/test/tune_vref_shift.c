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

/*! \file tune_vref_shift.c
 * @brief get nand VREF by histogram(new)
 *
 * \addtogroup test
 * \defgroup tune_vref_shift
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
#include "ndcu_reg_access.h"
#include "ncb_ndcu_register.h"
#include "eccu_reg_access.h"
#include "ncb_eccu_register.h"

/*! \cond PRIVATE */
#define __FILEID__ tsb_tune
#include "trace.h"
/*! \endcond */

extern void ficu_mode_ard_control(u32 mode);

#if XLC == 3
#define XLC_VREF_NUM	7
#elif XLC == 4
#define XLC_VREF_NUM	15
#endif

u32 get_err_cnt(void)
{
	u32 cnt_0to1;
	u32 cnt_1to0;

	mdec_ctrs_reg1_t mdec_err_reg;
	mdec_err_reg.all = mdec_readl(MDEC_CTRS_REG1);
	cnt_0to1 = mdec_err_reg.b.eccu_mdec_err_cnt0to1;
	cnt_1to0 = mdec_err_reg.b.eccu_mdec_err_cnt1to0;
	return (cnt_1to0 + cnt_0to1);
}

#if HAVE_TSB_SUPPORT
#include "nand_tsb.h"
#define NUM_EXCEPT_PAGES	0

u32 vref_cur_89;
u32 vref_cur_8a;
u8 vref_default[XLC_VREF_NUM] = {0, 0, 0, 0, 0, 0, 0};
u8 vref_page_type[XLC_VREF_NUM] = {0, 1, 2, 1, 0, 1, 2};
u8 vref_addr[XLC_VREF_NUM] = {0x89, 0x8A, 0x89, 0x8A, 0x89, 0x8A, 0x89};

void get_def_nand_vref(void)
{
	u32 i;

	ficu_mode_disable();
	ndcu_en_reg_control_mode();
	extern u32 get_feature(u8 ch, u8 ce, u8 fa);
	vref_cur_89 = get_feature(0, 0, 0x89);
	vref_cur_8a = get_feature(0, 0, 0x8A);
	ncl_test_trace(LOG_ERR, 0x7acd, "Default vref %x %x\n", vref_cur_89, vref_cur_8a);
	for (i = 0; i < 7; i++) {
		if (i & BIT0) {
			vref_default[i] = (vref_cur_8a >> (i/2*8)) & 0xFF;
		} else {
			vref_default[i] = (vref_cur_89 >> (i/2*8)) & 0xFF;
		}
	}
	ndcu_dis_reg_control_mode();
	ficu_mode_enable();
}

void set_nand_vref(u8 vref_idx, u8 vref_value)
{
	ficu_mode_disable();
	ndcu_en_reg_control_mode();
	extern void set_feature(u8 ch, u8 ce, u8 fa, u32 val);
	if (vref_idx & BIT0) {
		vref_idx >>= 1;
		vref_cur_8a &= ~(0xFF << (vref_idx*8));
		vref_cur_8a |= vref_value << (vref_idx*8);
		set_feature(0, 0, 0x8A, vref_cur_8a);
	} else {
		vref_idx >>= 1;
		vref_cur_89 &= ~(0xFF << (vref_idx*8));
		vref_cur_89 |= vref_value << (vref_idx*8);
		set_feature(0, 0, 0x89, vref_cur_89);
	}
	ndcu_dis_reg_control_mode();
	ficu_mode_enable();
}
#endif

#if HAVE_HYNIX_SUPPORT
#include "nand_sk.h"
#define NUM_EXCEPT_PAGES	0

u8 vref_default[XLC_VREF_NUM] = {0, 0, 0, 0, 0, 0, 0};
u8 vref_page_type[XLC_VREF_NUM] = {2, 1, 0, 1, 2, 1, 0};
u8 vref_addr[XLC_VREF_NUM] = {0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15};

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

void get_def_nand_vref(void)
{
	u32 i;

	ficu_mode_disable();
	ndcu_en_reg_control_mode();
	for (i = 0; i < XLC_VREF_NUM; i++) {
		vref_default[i] = get_param(0, 0, vref_addr[i]);
		ncl_test_trace(LOG_ERR, 0xa447, "Default vref %b %b\n", vref_addr[i], vref_default[i]);
	}
	ndcu_dis_reg_control_mode();
	ficu_mode_enable();
}

void set_nand_vref(u8 vref_idx, u8 vref_value)
{
	ficu_mode_disable();
	ndcu_en_reg_control_mode();
	set_param(0, 0, vref_addr[vref_idx], vref_value);
	ndcu_dis_reg_control_mode();
	ficu_mode_enable();
}
#endif

#if HAVE_UNIC_SUPPORT || HAVE_MICRON_SUPPORT
#define NUM_EXCEPT_PAGES	72

#include "nand_unic.h"
u32 vref_cur_89;
u32 vref_cur_8a;
u8 vref_default[XLC_VREF_NUM] = {0, 0, 0, 0, 0, 0, 0};
u8 vref_page_type[XLC_VREF_NUM] = {1, 2, 1, 0, 1, 2, 1};
u8 vref_addr[XLC_VREF_NUM] = {0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xAB};

void get_def_nand_vref(void)
{
	u32 i;

	ficu_mode_disable();
	ndcu_en_reg_control_mode();
	extern u32 get_feature(u8 ch, u8 ce, u8 fa);
	for (i = 0; i < 7; i++) {
		vref_default[i] = get_feature(0, 0, vref_addr[i]);
		ncl_test_trace(LOG_ERR, 0xb9bc, "Default vref %b %b\n", vref_addr[i], vref_default[i]);
	}
	ndcu_dis_reg_control_mode();
	ficu_mode_enable();
}

void set_nand_vref(u8 vref_idx, u8 vref_value)
{
	ficu_mode_disable();
	ndcu_en_reg_control_mode();
	extern void set_feature(u8 ch, u8 ce, u8 fa, u32 val);
	set_feature(0, 0, vref_addr[vref_idx], vref_value);
	ndcu_dis_reg_control_mode();
	ficu_mode_enable();
}
#endif

#if HAVE_SANDISK_SUPPORT
#include "nand_sndk.h"
#define NUM_EXCEPT_PAGES	0

u32 vref_cur_12;
u32 vref_cur_13;
u8 vref_default[XLC_VREF_NUM] = {0, 0, 0, 0, 0, 0, 0};
u8 vref_page_type[XLC_VREF_NUM] = {0, 1, 2, 1, 0, 1, 2};
u8 vref_addr[XLC_VREF_NUM] = {0x12, 0x13, 0x12, 0x13, 0x12, 0x13, 0x12};

void get_def_nand_vref(void)
{
	u32 i;

	ficu_mode_disable();
	ndcu_en_reg_control_mode();
	extern u32 get_feature(u8 ch, u8 ce, u8 fa);
	vref_cur_12 = get_feature(0, 0, 0x12);
	vref_cur_13 = get_feature(0, 0, 0x13);
	ncl_test_trace(LOG_ERR, 0x54fc, "Default vref %x %x\n", vref_cur_12, vref_cur_13);
	for (i = 0; i < 7; i++) {
		if (i & BIT0) {
			vref_default[i] = (vref_cur_13 >> (i/2*8)) & 0xFF;
		} else {
			vref_default[i] = (vref_cur_12 >> (i/2*8)) & 0xFF;
		}
	}
	ndcu_dis_reg_control_mode();
	ficu_mode_enable();
}

void set_nand_vref(u8 vref_idx, u8 vref_value)
{
	ficu_mode_disable();
	ndcu_en_reg_control_mode();
	extern void set_feature(u8 ch, u8 ce, u8 fa, u32 val);
	if (vref_idx & BIT0) {
		vref_idx >>= 1;
		vref_cur_13 &= ~(0xFF << (vref_idx*8));
		vref_cur_13 |= vref_value << (vref_idx*8);
		set_feature(0, 0, 0x13, vref_cur_13);
	} else {
		vref_idx >>= 1;
		vref_cur_12 &= ~(0xFF << (vref_idx*8));
		vref_cur_12 |= vref_value << (vref_idx*8);
		set_feature(0, 0, 0x12, vref_cur_12);
	}
	ndcu_dis_reg_control_mode();
	ficu_mode_enable();
}
#endif

void tune_vref(void)
{
	u32 pg_type;// Low/middle/upper
	u32 xlc_wl_cnt;
	u32 vref_cnt;
	u8 vref_idx[4];
	u8 vref_val[4];

	log_level_chg(LOG_ALW);
	ficu_mode_ard_control(0);
	eccu_set_dec_mode(DEC_MODE_MDEC);
	eccu_cfg.b.pdec = 0;
	xlc_wl_cnt = (nand_page_num() - NUM_EXCEPT_PAGES) / XLC;
	extern bool erase_pda(pda_t pda, nal_pb_type_t pb_type);
	extern bool write_pda_page(pda_t pda, nal_pb_type_t pb_type, u32 step);
	extern bool read_pda_du(pda_t pda, nal_pb_type_t pb_type, u32 check_level, u8 du_fmt, bool fail_retry);
	//erase_pda(0, 1);
	u32 pg;
	for (pg = 0; pg < nand_page_num(); pg++) {
		//write_pda_page(pg << nand_info.pda_page_shift, 1, 0);
	}
	u32 cnt_flip = 0;
	u8 cur_shift;
	u32 step;
	u32 loop;
	bool ret;
#if HAVE_SANDISK_SUPPORT
	int i;
	nf_ncmd_fmt_ptr_t reg_ptr;
	nf_ncmd_fmt_reg0_t ndcmd_fmt;
	reg_ptr.all = 0;

	for (i = FINST_NAND_FMT_XLC_READ_LOW; i <= FINST_NAND_FMT_XLC_READ_UPR; i++) {
		reg_ptr.b.nf_ncmd_fmt_cfg_ptr = i;
		ndcu_writel(reg_ptr.all, NF_NCMD_FMT_PTR);
		ndcmd_fmt.all = ndcu_readl(NF_NCMD_FMT_REG0);
		ndcmd_fmt.b.prefix_cmd1 = NAND_FMT_PREFIX_CMD3;
		ndcu_writel(ndcmd_fmt.all, NF_NCMD_FMT_REG0);
	}
#endif

	get_def_nand_vref();
	//for (pg_type = 0; pg_type < 3; pg_type++) {
	for (pg_type = 0; pg_type < 3; pg_type++) {
		vref_cnt = 0;
		for (loop = 0; loop < XLC_VREF_NUM; loop++) {
			if (vref_page_type[loop] != pg_type) {
				continue;
			}
			vref_idx[vref_cnt] = loop;
			vref_val[vref_cnt] = vref_default[loop];
			vref_cnt++;
		}
		switch (pg_type) {
		case 0:
			ncl_test_trace(LOG_ERR, 0xcf2d, "Low page\n");
			break;
		case 1:
			ncl_test_trace(LOG_ERR, 0xf184, "Middle page\n");
			break;
		case 2:
			ncl_test_trace(LOG_ERR, 0xdb15, "Upper page\n");
			break;
		}
		ncl_test_trace(LOG_ERR, 0x5616, "Step 0:");
		for (loop = 0; loop < vref_cnt; loop++) {
			ncl_test_trace(LOG_ERR, 0x8cda, "    FA %d: %b %b,", vref_idx[loop], vref_addr[vref_idx[loop]], vref_val[loop]);
		}
		ncl_test_trace(LOG_ERR, 0xb7f6, "\n");

		u32 pass_cnt = 0, pass_cnt_max = 0;
		while(1) {
			for (loop = 0; loop < vref_cnt; loop++) {
				u32 best_shift;
				cur_shift = vref_val[loop];
				best_shift = cur_shift;
				step = 0;
				u32 i;
				for (i = 0; i < 26; i++) {
					cnt_flip = 0;
					pass_cnt = 0;
					set_nand_vref(vref_idx[loop], cur_shift);
					for (pg = 0; pg < nand_page_num(); pg ++) {
						if (get_xlc_page_type(pg) != pg_type) {
							continue;
						}
						ret = read_pda_du(pg << nand_info.pda_page_shift, 1, 1, DU_FMT_USER_4K, false);
						if (!ret) {
							break;
						}
						pass_cnt++;
						if (pass_cnt > pass_cnt_max) {
							pass_cnt_max = pass_cnt;
							best_shift = cur_shift;
						}
						cnt_flip += get_err_cnt();
					}
					//ncl_test_trace(LOG_ERR, 0, "shift %d, pass cnt %d\n", cur_shift, pass_cnt);
					if (pass_cnt_max == xlc_wl_cnt) {
						break;
					}
					step += 10;
					if (((step / 10) % 2) == 1) {
						cur_shift += step;
					} else {
						cur_shift -= step;
					}
				}
				vref_val[loop] = best_shift;
				set_nand_vref(vref_idx[loop], best_shift);
				if (pass_cnt_max == xlc_wl_cnt) {
					break;
				}
			}
			if (pass_cnt_max == xlc_wl_cnt) {
				break;
			} else {
				// Not found a vref group read all success
				sys_assert(0);
			}
		}
		ncl_test_trace(LOG_ERR, 0xd165, "Step 2:");
		for (loop = 0; loop < vref_cnt; loop++) {
			ncl_test_trace(LOG_ERR, 0xd185, "    FA %d: %b %b,", vref_idx[loop], vref_addr[vref_idx[loop]], vref_val[loop]);
		}
		ncl_test_trace(LOG_ERR, 0xb8df, "\n");

		for (loop = 0; loop < vref_cnt; loop++) {
			//ncl_test_trace(LOG_ERR, 0, "---------\n");
			cur_shift = vref_val[loop];
			step = 10;
			//ncl_test_trace(LOG_ERR, 0, "cur 0x%b, %d\n", cur_shift, cnt_flip);

			while (1) {
				u32 i;
				bool worse = false;
				bool pworse = false;
				bool nworse = false;
				u32 cnt_flip_tmp = 0;
				u32 cnt_flip_p = 0;
				u32 cnt_flip_n = 0;

				for (i = 0; i < 2; i++) {
					worse = false;
					cnt_flip_tmp = 0;
					if (i == 0) {
						set_nand_vref(vref_idx[loop], cur_shift + step);
					} else {
						set_nand_vref(vref_idx[loop], cur_shift - step);
					}

					for (pg = 0; pg < nand_page_num(); pg ++) {
						if (get_xlc_page_type(pg) != pg_type) {
							continue;
						}
						ret = read_pda_du(pg << nand_info.pda_page_shift, 1, 1, DU_FMT_USER_4K, false);
						if (!ret) {
							worse = true;
							cnt_flip_tmp = ~0;
							break;
						} else {
							cnt_flip_tmp += get_err_cnt();
						}
						if (cnt_flip_tmp > cnt_flip) {
							worse = true;
							break;
						}
					}
					if (i == 0) {
						pworse = worse;
						cnt_flip_p = cnt_flip_tmp;
					} else {
						nworse = worse;
						cnt_flip_n = cnt_flip_tmp;
					}
				}
				if (pworse ^ nworse) {
					if (pworse) {// Shift negtive
						cur_shift -= step;
						cnt_flip = cnt_flip_n;
						//ncl_test_trace(LOG_ERR, 0, "<-- %d 0x%b\n", cnt_flip_n, cur_shift);
					} else if (nworse) {// Shift postive
						cur_shift += step;
						cnt_flip = cnt_flip_p;
						//ncl_test_trace(LOG_ERR, 0, "--> %d 0x%b\n", cnt_flip_p, cur_shift);
					} else {
						sys_assert(0);
					}
				} else {
					if (pworse) {
					} else {
						if (cnt_flip_n < cnt_flip_p) {// Shift negtive
							cur_shift -= step;
							cnt_flip = cnt_flip_n;
							//ncl_test_trace(LOG_ERR, 0, "<- %d\n", cnt_flip_n);
						} else {// Shift postive
							cur_shift += step;
							cnt_flip = cnt_flip_p;
							//ncl_test_trace(LOG_ERR, 0, "-> %d\n", cnt_flip_p);
						}
					}
				}

				if (step == 1) {
					break;
				}
				step = (step + 1) / 2;
			}
			//ncl_test_trace(LOG_ERR, 0, "Final fa 0x%b, vref 0x%b!!!!\n", vref_addr[vref_idx[loop]], cur_shift);
			vref_val[loop] = cur_shift;
			set_nand_vref(vref_idx[loop], cur_shift);
		}
		ncl_test_trace(LOG_ERR, 0x13b4, "Step 3:");
		for (loop = 0; loop < vref_cnt; loop++) {
			ncl_test_trace(LOG_ERR, 0x85ea, "    FA %d: %b %b,", vref_idx[loop], vref_addr[vref_idx[loop]], vref_val[loop]);
		}
		ncl_test_trace(LOG_ERR, 0x5cec, "\n");
		ncl_test_trace(LOG_ERR, 0xe4c7, "err bits: %d\n", cnt_flip);
		ncl_test_trace(LOG_ERR, 0x9e5c, "------------\n");
	}
}

/*
Usage:
1. add ncl_20/cmakelists.txt
	${SRC_ROOT}/${BE_MOD}/test/tune_vref_shift.c
2. comment error bits count print in read_pda_du()
	//ncl_test_trace(LOG_ERR, 0, "err bits %d + %d = %d\n", cnt_1to0, cnt_0to1, cnt_1to0 + cnt_0to1);
3. Add below code in ncl_init()
	extern void tune_vref(void);
	tune_vref();
	sys_assert(0);
4. Pay attention to "Final fa 0x**h, vref 0x**h!!!!" result
*/

/*! @} */

