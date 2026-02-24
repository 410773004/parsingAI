//-----------------------------------------------------------------------------
//                 Copyright(c) 2016-2020 Innogrit Corporation
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
/*! \file dfi_pad_cal_seq.c
 * @brief dfi pad calibration sequence APIs
 *
 * \addtogroup utils
 * \defgroup dfi
 * \ingroup utils
 * @{
 */
//=============================================================================

//-----------------------------------------------------------------------------
//  Include files:
//-----------------------------------------------------------------------------
#include "sect.h"
#include "dfi_init.h"
#include "dfi_common.h"
#include "dfi_reg.h"
#include "misc.h"
#include "stdio.h"

#define __FILEID__ dfipad
#include "trace.h"
//-----------------------------------------------------------------------------
//  Macros definitions:
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
//  Data type definitions: typedef, struct or class
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
//  Data declaration: Private or Public:
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
//  Extern Data or Functions declaration:
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
//  Functions:
//-----------------------------------------------------------------------------
dfi_code u8 check_failure(u8 p_n)
{
	u8 try = 9;
	u8 cnt = 0;

	if ( p_n == 0 ) { // Check P calibration
		vgen_ctrl_0_t vgen = { .all = dfi_readl(VGEN_CTRL_0), };
		vgen.b.vgen_bypass = 0;
		u8 vgen_vsel_org = vgen.b.vgen_vsel;
		u8 vgen_range_org = vgen.b.vgen_range;
		vgen.b.vgen_vsel = 0x1; // 40%
		vgen.b.vgen_range = 1;
		dfi_writel(vgen.all, VGEN_CTRL_0);

		ndelay(1000);
		for (u8 i=0; i<try; i++) {
			udelay(1);
			cnt = cnt + (dfi_readl(IO_CAL_1) & 1);
		}

		vgen.b.vgen_bypass = 1;
		vgen.b.vgen_vsel = vgen_vsel_org;
		vgen.b.vgen_range = vgen_range_org;
		dfi_writel(vgen.all, VGEN_CTRL_0);

		if (cnt < try/2) {
			return 1; // P Calibraion failed
		}

	}else{ // Check N calibration
		vgen_ctrl_0_t vgen = { .all = dfi_readl(VGEN_CTRL_0), };
		vgen.b.vgen_bypass = 0;
		u8 vgen_vsel_org = vgen.b.vgen_vsel;
		u8 vgen_range_org = vgen.b.vgen_range;
		vgen.b.vgen_vsel = 0x1d;
		vgen.b.vgen_range = 2;
		dfi_writel(vgen.all, VGEN_CTRL_0);

		ndelay(1000);
		for (u8 i=0; i<try; i++) {
			udelay(1);
			cnt = cnt + (dfi_readl(IO_CAL_1) & 1);
		}

		vgen.b.vgen_bypass = 1;
		vgen.b.vgen_vsel = vgen_vsel_org;
		vgen.b.vgen_range = vgen_range_org;
		dfi_writel(vgen.all, VGEN_CTRL_0);

		if (cnt > try/2) {
			return 2; // N Calibraion failed
		}
	}

	return 0;
}

dfi_code int dfi_pad_cal_seq(u8 debug)
{
#ifdef DDR_PERF_CNTR
	u32 pc_val;
	u16 pc_val_up;
	u8 pc_over;
	mc_pc_clk_start(0);
#endif

	int pstr = -1;
	int nstr = -1;
	u8 fail = 0;
	u8 try = 9;
	u8 floor = 8; //4-4-2-1-1, at least 4 fingers are required

	//Switch to half VDDQ for Calibraion
	vgen_ctrl_0_t vgen = { .all = dfi_readl(VGEN_CTRL_0), };
	u8 vgen_range_org = vgen.b.vgen_range;
	u8 vgen_vsel_org = vgen.b.vgen_vsel;
	vgen.b.vgen_range = 0x2; // Set to 50% of VDDQ
	vgen.b.vgen_vsel = 0xa;
	//vgen.b.vgen_bypass = 1;
	dfi_writel(vgen.all, VGEN_CTRL_0);
	ndelay(500000); // Must wait for VREF stable
	io_cal_0_t cal0 = { .all = 0 };
	dfi_writel(cal0.all, IO_CAL_0);

	for (u8 i = 0; i < 2; i++) {
		if (i == 0) {
			if (debug > 2)
				utils_dfi_trace(LOG_ERR, 0x9982, "Pad Calibration - Start P side Calibration\n");

		 	cal0.all = dfi_readl(IO_CAL_0);
			cal0.b.cal_en_p = 1;
			cal0.b.cal_en_n = 0;
			dfi_writel(cal0.all, IO_CAL_0);
		} else {
			if (debug > 2)
				utils_dfi_trace(LOG_ERR, 0x44c4, "Pad Calibration - Start N side Calibration\n");

			cal0.all = dfi_readl(IO_CAL_0);
			cal0.b.cal_en_p = 0;
			cal0.b.cal_en_n = 1;
			dfi_writel(cal0.all, IO_CAL_0);
		}
		// rainier_a0 errata - use str_max=31 of inverted NSTR on reset/alert pads.
		// For all other projects, use str_max=32.
		u8 str_max = 31;
		for (u8 config = 0; config <= str_max; config += 2) {
			if (14 < config && config < 24) {
				continue; //skip invalid configurations
			}
			if (config == str_max)
				config --; //specical case, max value is 31

			if (i == 0) { //P side Calibration
				cal0.all = dfi_readl(IO_CAL_0);
				cal0.b.cal_pstr = config;
				dfi_writel(cal0.all, IO_CAL_0);

				u8 cnt = 0;
				for (u8 m=0; m<try; m++) {
					ndelay(1000);
					cnt = cnt + (dfi_readl(IO_CAL_1) & 1);
				}

				if (cnt > try/2) {
					pstr = config;
					if (debug > 1)
						utils_dfi_trace(LOG_ERR, 0x9072, "Pad Calibration - P Calibration: %d.\n", pstr);
					if (pstr < floor) {
						pstr = floor;
						if (debug > 1)
							utils_dfi_trace(LOG_ERR, 0xfbec, "Pad Calibration - P Calibration set to floor: %d.\n", pstr);
					}
					break;
				}
			} else {
				cal0.all = dfi_readl(IO_CAL_0);
				cal0.b.cal_nstr = config;
				dfi_writel(cal0.all, IO_CAL_0);

				u8 cnt = 0;
				for (u8 m=0; m<try; m++) {
					ndelay(1000);
					cnt = cnt + (dfi_readl(IO_CAL_1) & 1);
				}

				if (cnt <= try/2) {
					nstr = config;
					if (debug > 1)
						utils_dfi_trace(LOG_ERR, 0x5764, "Pad Calibration - N Calibration: %d.\n", nstr);
					if (nstr < floor) {
						nstr = floor;
						if (debug > 1)
							utils_dfi_trace(LOG_ERR, 0xec75, "Pad Calibration - N Calibration set to floor: %d.\n", nstr);
					}
					break;
				}
			}

		}
		if (i == 0 && pstr == -1) {
			pstr = 31;
			fail = check_failure(i);
			if ( fail != 0 )
				break;
		}
		if (i == 1 && nstr == -1) {
			nstr = str_max - 1;
			fail = check_failure(i);
			if ( fail != 0 )
				break;
		}
	}

	cal0.all = dfi_readl(IO_CAL_0);
	cal0.b.cal_en_n = 0;
	cal0.b.cal_en_p = 0;
	dfi_writel(cal0.all, IO_CAL_0);


	if (debug >= 1 && fail == 0) {
	//	utils_dfi_trace(LOG_ERR, 0, "Pad Calibration - Finish Calibration with PSTR=%d, NSTR=%d\n", pstr, nstr);
		u8 pstr_dec;
		u8 nstr_dec;
		pstr_dec = (pstr & 1) + ((pstr & 2)>>1) + (((pstr & 4)>>2)<<1) + (((pstr & 8)>>3)<<2) + (((pstr & 16)>>4)<<2);
		nstr_dec = (nstr & 1) + ((nstr & 2)>>1) + (((nstr & 4)>>2)<<1) + (((nstr & 8)>>3)<<2) + (((nstr & 16)>>4)<<2);
		utils_dfi_trace(LOG_ERR, 0x7470, "Pad Calibration - Finish Calibration with P=%d (%d fingers), N=%d (%d fingers)\n", pstr, pstr_dec, nstr, nstr_dec);
	}



	io_data_0_t data0 = { .all = dfi_readl(IO_DATA_0), };
	data0.b.data_pstr = pstr;
	data0.b.data_nstr = nstr;
	dfi_writel(data0.all, IO_DATA_0);

	io_adcm_0_t adcm0 = { .all = dfi_readl(IO_ADCM_0), };
	adcm0.b.adcm_pstr = pstr;
	adcm0.b.adcm_nstr = nstr;
	dfi_writel(adcm0.all, IO_ADCM_0);

	//Restore original VGEN setting
	//vgen.b.vgen_bypass = 0;
	vgen.b.vgen_range = vgen_range_org;
	vgen.b.vgen_vsel = vgen_vsel_org;
	dfi_writel(vgen.all, VGEN_CTRL_0);
	ndelay(10000);


#ifdef DDR_PERF_CNTR
	mc_pc_clk_stop(0);
	mc_pc_clk_get(0, &pc_val, &pc_val_up, &pc_over);
	utils_dfi_trace(LOG_ERR, 0x382f, "Pad Calibration - Total cycles take: %d.\n", pc_val);
#endif

	if (fail == 1) {
		utils_dfi_trace(LOG_ERR, 0x4c3b, "Pad Calibration - Error, P Calibration failed.\n");
		return -1;
	} else if (fail == 2) {
		utils_dfi_trace(LOG_ERR, 0xffbd, "Pad Calibration - Error, N Calibration failed with P Calibration = %d.\n", pstr);
		return -1;
	} else{
		return 0;
	}
}

#if (ENABLE_DFI_VALIDATION_DEBUG == 1)
// Validation-use code
// Loop dfi_pad_cal_seq multiple times
// Can run this test for a large numer of loops while applying external changes such as temperature
dfi_code void dfi_pad_cal_seq_loop(u32 loop)
{
	int cur_res = 0;
	u8 debug = 0;
	u8 pstr = 0;
	u8 nstr = 0;
	u32 pstr_res[32];
	u32 nstr_res[32];

	for(u8 i = 0; i < 32; i++){
		pstr_res[i] = 0;
		nstr_res[i] = 0;
	}

	for (u32 l = 0; l < loop; l++) {
		if (l%10000 == 0)
			utils_dfi_trace(LOG_ERR, 0x296b, "On loop %d...\n", l);

		cur_res = dfi_pad_cal_seq(debug);

		io_data_0_t data0 = { .all = dfi_readl(IO_DATA_0), };
		pstr = data0.b.data_pstr;
		nstr = data0.b.data_nstr;

		if (cur_res != 0){
			utils_dfi_trace(LOG_ERR, 0x1410, "dfi_pad_cal_seq_loop failed on loop = %d.\n", l);
			u8 pstr_dec = (pstr & 1) + ((pstr & 2)>>1) + (((pstr & 4)>>2)<<1) + (((pstr & 8)>>3)<<2) + (((pstr & 16)>>4)<<2);
			u8 nstr_dec = (nstr & 1) + ((nstr & 2)>>1) + (((nstr & 4)>>2)<<1) + (((nstr & 8)>>3)<<2) + (((nstr & 16)>>4)<<2);
			utils_dfi_trace(LOG_ERR, 0x6764, "Unexpected results: P=%d (%d fingers), N=%d (%d fingers)\n", pstr, pstr_dec, nstr, nstr_dec);
			return;
		}

		pstr_res[pstr] ++;
		nstr_res[nstr] ++;
	}

	utils_dfi_trace(LOG_ERR, 0xc234, "dfi_pad_cal_seq_loop results summary:\n");
	for(u8 i = 0; i < 32; i++){
		u8 dec = (i & 1) + ((i & 2)>>1) + (((i & 4)>>2)<<1) + (((i & 8)>>3)<<2) + (((i & 16)>>4)<<2);
		if (pstr_res[i] != 0)
			utils_dfi_trace(LOG_ERR, 0x5c86, "PSTR result = %d, (%d fingers) happened %d out of %d.\n", i, dec, pstr_res[i], loop);
		if (nstr_res[i] != 0)
			utils_dfi_trace(LOG_ERR, 0x5c97, "NSTR result = %d, (%d fingers) happened %d out of %d.\n", i, dec, nstr_res[i], loop);
	}

	utils_dfi_trace(LOG_ERR, 0x43ca, "dfi_pad_cal_seq_loop end.\n");
}
#endif

/*! @} */
