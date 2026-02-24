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
/*! \file dfi_calvl_seq.c
 * @brief dfi commmand bus level training
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
#include "dfi_config.h"
#include "dfi_reg.h"
#include "mc_reg.h"
#include "mc_config.h"
#include "stdio.h"

#define __FILEID__ dficalvl
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
dfi_code void dfi_mc_calvl_cbt_mode_seq(bool en, u8 cs)
{
	device_config2_t config2 = { .all = mc0_readl(DEVICE_CONFIG2), };
	config2.b.cbt = en;
	mc0_writel(config2.all, DEVICE_CONFIG2);

	dfi_mc_mr_rw_req_seq(MR_WRITE, MR13, cs);
}

dfi_code void dfi_mc_cke_pull_down_mode(bool en, u8 cs)
{
	dram_calibration_cmd_t calibration = { .all = mc0_readl(DRAM_CALIBRATION_CMD), };
	calibration.b.dram_calibration_cs_sel = cs;
	calibration.b.cke_pull_down_mode = en;
	mc0_writel(calibration.all, DRAM_CALIBRATION_CMD);
}

dfi_code void dfi_calvl_update_ca_wr(u16 dly_cnt)
{
	dfi_ck_gate_adcm(CK_GATE_CS_ALL);

	sel_oadly_0_t oadly0 = { .all = dfi_readl(SEL_OADLY_0), };
	oadly0.b.ca_wr_dly = dly_cnt;
	dfi_writel(oadly0.all, SEL_OADLY_0);

	dfi_ck_gate_adcm(CK_GATE_NORMAL);
}

dfi_code void dfi_calvl_update_ck_wr(u8 cs, u16 dly_cnt)
{
	dfi_ck_gate_ck(cs);

	sel_oadly_1_t oadly1 = { .all = dfi_readl(SEL_OADLY_1), };
	oadly1.b.ck_wr_dly   = dly_cnt;
	dfi_writel(oadly1.all, SEL_OADLY_1);

	dfi_ck_gate_ck(CK_GATE_NORMAL);
}

dfi_code void dfi_calvl_update_rank_wr(u8 cs, u16 dly_cnt)
{
	dfi_ck_gate_rank(cs);

	sel_oadly_2_t oadly2 = { .all = dfi_readl(SEL_OADLY_2), };
	oadly2.b.rank_wr_dly = dly_cnt;
	dfi_writel(oadly2.all, SEL_OADLY_2);

	dfi_ck_gate_rank(CK_GATE_NORMAL);
}

dfi_code int dfi_calvl_issue_dqs(void)
{
	u32 rdata;
	lvl_all_wo_0_t lvlwo = { .all = dfi_readl(LVL_ALL_WO_0), };
	lvlwo.b.calvl_send_dqs = 1;
	dfi_writel(lvlwo.all, LVL_ALL_WO_0);
	//calvl_send_dqs is Auto_CLR

	ndelay(1000);

	//poll for send_dqs_done
	for (int i = 0; i<10000; i++) {
		rdata = dfi_readl(LVL_ALL_RO_0);
		if (rdata & CALVL_SEND_DQS_DONE_MASK)
			return 0;
	}

	utils_dfi_trace(LOG_ERR, 0x0302, "dfi_calvl_issue_dqs - DQS send failed!\n");
	return -1;
}

dfi_code int dfi_calvl_issue_pattern(void)
{
	u32 rdata;

	lvl_all_wo_0_t lvlwo   = { .all = dfi_readl(LVL_ALL_WO_0), };
	lvlwo.b.calvl_send_pat = 1;
	dfi_writel(lvlwo.all, LVL_ALL_WO_0);
	//calvl_send_pat is Auto_CLR

	ndelay(1000);

	//poll for send_pulse_done
	for (int i = 0; i<10000; i++) {
		rdata = dfi_readl(LVL_ALL_RO_0);
		if (rdata & CALVL_SEND_PAT_DONE_MASK)
			return 0;
	}

	utils_dfi_trace(LOG_ERR, 0x07c4, "dfi_calvl_issue_pattern - CA pattern send failed!\n");
	return 0;
}

dfi_code int dfi_calvl_seq(u8 target_cs, u32 target_speed, u8 debug)
{
	u8 skip_ca_sweep = 0;
	u8 cs;

	switch (target_cs) {
	case 0:
		cs = CS0_BIT;
		break;
	case 1:
		cs = CS1_BIT;
		break;
	case 2:
		cs = CS2_BIT;
		break;
	case 3:
		cs = CS3_BIT;
		break;
	default:
		cs = CS0_BIT;
	}

	if (debug)
		utils_dfi_trace(LOG_ERR, 0x987e, "DFI CA leveling - Start sequence, target CS [%d]\n", target_cs);

	u8 ca_state_exp0 = 0x15;//0001 0101
	u8 ca_state_exp1 = 0x3f;//0010 1010
	u8 ca_state_exp0n = (~ca_state_exp0);//1100 0000
	u8 ca_state_exp1n = (~ca_state_exp1);//1101 0101

	//program training pattern 0
	sel_ctrl_0_t ctrl0    = { .all = dfi_readl(SEL_CTRL_0), };
	ctrl0.b.sel_calvl_pat = 0;
	dfi_writel(ctrl0.all,SEL_CTRL_0);

	sel_calvl_pat_0_t pat0 = { .all = dfi_readl(SEL_CALVL_PAT_0), };
	pat0.b.calvl_pat_bg    = ca_state_exp0n;
	pat0.b.calvl_pat_fg    = ca_state_exp0;
	dfi_writel(pat0.all,SEL_CALVL_PAT_0);

	//program training pattern 1
	ctrl0.b.sel_calvl_pat = 1;
	dfi_writel(ctrl0.all,SEL_CTRL_0);

	pat0.b.calvl_pat_bg    = ca_state_exp1n;
	pat0.b.calvl_pat_fg    = ca_state_exp1;
	dfi_writel(pat0.all,SEL_CALVL_PAT_0);

	//Config frequency set point 1
	device_config2_t config2 = { .all = mc0_readl(DEVICE_CONFIG2), };
	config2.b.fsp_wr = 1;
	mc0_writel(config2.all, DEVICE_CONFIG2);

	dfi_mc_mr_rw_req_seq(MR_WRITE, MR13, CS_ALL_BITS); //Write to LP4 MR13, switch FSP_WR

	mc_switch_latency(target_speed); // program CL, CWL, nWR
	device_config0_t config0 = { .all = mc0_readl(DEVICE_CONFIG0) };
	config0.b.wl_select         = 0;
	mc0_writel(config0.all,DEVICE_CONFIG0);

	dfi_phy_cntl1_t cntl1  = { .all = mc0_readl(DFI_PHY_CNTL1), };
	cntl1.b.trddata_en     = config0.b.cas_latency - 2; // more than 2 affects rdlvl_gate
	cntl1.b.tphy_wrlat = config0.b.cas_write_latency;
	mc0_writel(cntl1.all,DFI_PHY_CNTL1);

	ndelay(100);
	dfi_mc_mr_rw_req_seq(MR_WRITE, MR1, CS_ALL_BITS); //Write to LP4 MR1

	ndelay(100);
	dfi_mc_mr_rw_req_seq(MR_WRITE, MR2, CS_ALL_BITS); //Write to LP4 MR2

	ndelay(100);
	dfi_mc_mr_rw_req_seq(MR_WRITE, MR3, CS_ALL_BITS); //Write to LP4 MR3

	ndelay(100);
	dfi_mc_mr_rw_req_seq(MR_WRITE, MR11, CS_ALL_BITS); //Write to LP4 MR11

	u16 setup_delay_f[6];
	u16 hold_delay_f[6];
	u16 setup_delay[6];
	u16 hold_delay[6];

	for (u8 i=0; i<6; i++) {
		setup_delay_f[i] = 0;
		hold_delay_f[i] = 0;
		setup_delay[i] = 0;
		hold_delay[i] = 0;
	}

	//If skip_ca_sweep==1, skip CA CK delay sweep and vref_value_start, vref_range_start will be the final value programed to DRAM
	u8 vref_value_start = 0x00; //0x19;
	u8 vref_range_start = 1;
	u8 vref_f = vref_value_start;
	u8 eye_min_global = 0;

	//If skip_ca_sweep==1, skip CA CK delay sweep
	if (skip_ca_sweep==0) {
		//Sweep CA/CK eye
		u8 sweep_setup = 1;
		u8 sweep_hold = 0;
		u32 rdata;
		u8 dq_state0;
		u8 dq_state1;
		u8 ca_state;
		u8 eye_min_local = 0;
		u16 dly_cnt = 0;
		u16 dly_cnt_max = 255;
		u8 setup_flag = 0;
		u8 hold_flag = 0;

		sel_ctrl_1_t sel1     = { .all = dfi_readl(SEL_CTRL_1), };
		sel1.b.simul_oadly_ca = 1;
		//Currently only support 1 rank
		sel1.b.simul_oadly_ck = 1;
		sel1.b.simul_oadly_rank = 1;
		dfi_writel(sel1.all,SEL_CTRL_1);

		device_config2_t config2 = { .all = mc0_readl(DEVICE_CONFIG2), };
		lvl_calvl_0_t calvl0        = { .all = dfi_readl(LVL_CALVL_0), };
		calvl0.b.calvl_tadr         = 0x3f; //CA Leveling Timier tADR, in unit of DFI dclk period
		calvl0.b.calvl_cs           = cs;
		calvl0.b.calvl_send_pat_sel = 0x1; //one-hot coded to select training patterns
		calvl0.b.calvl_vref_range   = vref_range_start;
		calvl0.b.calvl_vref_level   = vref_value_start;

		//Per VREF sweep
		for (u8 vref=vref_value_start; vref<=vref_value_start+30; vref++) {
			//Config current VREF for FSP_WR==1
			dfi_mc_vref_set(VREF_SET_CA, vref_range_start, vref, cs, debug);
			//Issue MRW to DRAM to CBT mode
			config2.b.cbt = 1;
			mc0_writel(config2.all,DEVICE_CONFIG2);
			dfi_mc_mr_rw_req_seq(MR_WRITE, MR13, cs);
			//Put DFI to CBT mode
			calvl0.b.calvl_mode  = 1;
			dfi_writel(calvl0.all,LVL_CALVL_0);
			//Set CKE low
			dfi_mc_cke_pull_down_mode(true, cs);
			ndelay(1000);
			//When CKE is driven Low, Upon training entry, the device will automatically switch to FSP 1
			//Change to target speed and sync.
			dfi_set_pll_freq(target_speed, false);
			dfi_sync();
			ndelay(1000);

			sweep_setup = 1;
			sweep_hold = 0;
			eye_min_local = 0;
			dly_cnt = 0;
			setup_flag = 0;
			hold_flag = 0;
			//Clear all ADCM output delay
			dfi_calvl_update_ca_wr(dly_cnt);
			dfi_calvl_update_ck_wr(CK_GATE_CS_ALL, dly_cnt);
			dfi_calvl_update_rank_wr(CK_GATE_CS_ALL, dly_cnt);

			if (debug)
				utils_dfi_trace(LOG_ERR, 0xd411, "DFI CA leveling - Current VREF_VALUE=%d, VREF_RANGE=%d\n", vref, vref_range_start);

			do {
				calvl0.b.calvl_send_pat_sel = 0x1;
				dfi_writel(calvl0.all,LVL_CALVL_0);
				dfi_calvl_issue_pattern();
				ndelay(100);
				rdata = dfi_readl(DQ_STATUS_ALL_0);
				dq_state0 = ((rdata>>8) & 0x3f);
				calvl0.b.calvl_send_pat_sel = 0x2;
				dfi_writel(calvl0.all,LVL_CALVL_0);
				dfi_calvl_issue_pattern();
				ndelay(100);
				rdata = dfi_readl(DQ_STATUS_ALL_0);
				dq_state1 = ((rdata>>8) & 0x3f);
				ca_state = ((~(dq_state0^ca_state_exp0)) & (~(dq_state1^ca_state_exp1)) & 0x3f);
				if (sweep_setup==1) {
					for (int i=0; i<6; i++) {//CA 0~5, so i is from 0 to 5
					if ( ((ca_state>>i) & 1) ==0) {//First failure means end of setup margin
						if ( ((setup_flag>>i) & 1) ==0) {
							if (debug)
								utils_dfi_trace(LOG_ERR, 0x7d6b, "DFI CA leveling - Sweep setup, dq_state0: 0x%x, dq_state1: 0x%x, ca_state: 0x%x, found CA%d setup: 0x%x.\n", dq_state0, dq_state1, ca_state, i, dly_cnt);
							setup_flag = (setup_flag | (1<<i));
							setup_delay[i] = dly_cnt;
						}
					}else if (dly_cnt == dly_cnt_max) {//Still pass at max delay line settings
						if ( ((setup_flag>>i) & 1) ==0) {
							if (debug)
								utils_dfi_trace(LOG_ERR, 0x6533, "DFI CA leveling - Sweep setup, dq_state0: 0x%x, dq_state1: 0x%x, ca_state: 0x%x, found CA%d setup: 0x%x (max).\n", dq_state0, dq_state1, ca_state, i, dly_cnt);
							setup_flag = (setup_flag | (1<<i));
							setup_delay[i] = dly_cnt;
						}
					}
					}
				}else{//Sweep hold
					for (int i=0; i<6; i++) {
					if ( ((ca_state>>i) & 1) ==0) {//First failure means end of hold margin
						if ( ((hold_flag>>i) & 1) ==0) {
							if (debug)
								utils_dfi_trace(LOG_ERR, 0x2829, "DFI CA leveling - Sweep hold, dq_state0: 0x%x, dq_state1: 0x%x, ca_state: 0x%x, found CA%d hold: 0x%x.\n", dq_state0, dq_state1, ca_state, i, dly_cnt);
							hold_flag = (hold_flag | (1<<i));
							hold_delay[i] = dly_cnt;
						}
					}else if (dly_cnt == dly_cnt_max) {//Still pass at max delay line settings
						if ( ((hold_flag>>i) & 1) ==0) {
							if (debug)
								utils_dfi_trace(LOG_ERR, 0x30a5, "DFI CA leveling - Sweep hold, dq_state0: 0x%x, dq_state1: 0x%x, ca_state: 0x%x, found CA%d hold: 0x%x (max).\n", dq_state0, dq_state1, ca_state, i, dly_cnt);
							hold_flag = (hold_flag | (1<<i));
							hold_delay[i] = dly_cnt;
						}
					}
				}
			}

			dly_cnt += 1;

			if (debug > 1)
				utils_dfi_trace(LOG_ERR, 0xc7da, "DFI CA leveling - set dly_cnt to: 0x%x.\n", dly_cnt);

			if (sweep_setup==1 && (dly_cnt <= dly_cnt_max)) {
				dfi_calvl_update_ca_wr(dly_cnt);//Push CA delay to check setup margin
			}else if (sweep_hold==1 && (dly_cnt <= dly_cnt_max)) {
				dfi_calvl_update_ck_wr(CK_GATE_CS_ALL, dly_cnt); //push CK delay to check hold margin
				dfi_calvl_update_rank_wr(CK_GATE_CS_ALL, dly_cnt);
			}

			if (sweep_setup==1 && (setup_flag == 0x3f || dly_cnt > dly_cnt_max)) {
				sweep_setup = 0;
				sweep_hold = 1;
				dly_cnt = 0;
				dfi_calvl_update_ca_wr(dly_cnt);
			}else if (sweep_hold==1 && (hold_flag == 0x3f || dly_cnt > dly_cnt_max)) {
				sweep_hold = 0;
				dfi_calvl_update_ck_wr(CK_GATE_CS_ALL, 0); //todo - restore previous value for untrained rank
				dfi_calvl_update_rank_wr(CK_GATE_CS_ALL, 0);
			}

			} while (sweep_setup==1 || sweep_hold==1);

			for (int i=0; i<6; i++) {
				if (eye_min_local==0 || (hold_delay[i]+setup_delay[i])<eye_min_local) {
					eye_min_local = hold_delay[i]+setup_delay[i];
				}
			}

			if (eye_min_local>eye_min_global) {
				eye_min_global = eye_min_local;
				vref_f = vref;
				for (int i=0; i<6; i++) {
					setup_delay_f[i] = setup_delay[i];
					hold_delay_f[i] = hold_delay[i];
				}
			}

			//change speed to 800 from target speed
			dfi_set_pll_freq(800, false);
			dfi_sync();
			ndelay(1000);
			//disable CKE force Low
			//device will automatically switch back tp FSP 0 after CKE high.
			dfi_mc_cke_pull_down_mode(false, cs);
			ndelay(1000);
			//DRAM exit CBT
			config2.b.cbt = 0;
			mc0_writel(config2.all,DEVICE_CONFIG2);
			dfi_mc_mr_rw_req_seq(MR_WRITE, MR13, cs);
			//DFI exit CBT
			calvl0.b.calvl_mode  = 0;
			dfi_writel(calvl0.all,LVL_CALVL_0);

		}
	}

	//---------------------------------------------------------------------
	// Rebalance setup vs hold and find the worst case margin
	//---------------------------------------------------------------------
	u16 setup_margin[6];
	u16 hold_margin[6];
	u16 setup_adj[6];
	u16 hold_adj_max=0;

	for (int i=0; i<6; i++) {
		setup_margin[i] = setup_delay_f[i];
		hold_margin[i] = hold_delay_f[i];

		if (debug)  {
			utils_dfi_trace(LOG_ERR, 0x4ec8, "DFI CA leveling - CA%d setup margin is: 0x%x.\n", i, setup_margin[i]);
			utils_dfi_trace(LOG_ERR, 0xfd75, "DFI CA leveling - CA%d hold margin is: 0x%x.\n", i, hold_margin[i]);
		}

		if (hold_margin[i]>setup_margin[i]) {
			if ( ((hold_margin[i]-setup_margin[i])>>1) > hold_adj_max) {
				hold_adj_max = ((hold_margin[i]-setup_margin[i])>>1);
			}
		}
	}

	if (debug)
		utils_dfi_trace(LOG_ERR, 0xbb65, "DFI CA leveling - Increasing CK delay by %d, rebalanced margins.\n", hold_adj_max);

	for (int i=0; i<6; i++) {
		setup_margin[i] = setup_margin[i] + hold_adj_max;
		hold_margin[i] = hold_margin[i] - hold_adj_max;
	}

	for (int i=0; i<6; i++) {
		if (setup_margin[i] > hold_margin[i]) {
			setup_adj[i] = ((setup_margin[i] - hold_margin[i])>>1);
			setup_margin[i] = setup_margin[i] - setup_adj[i];
			hold_margin[i] = hold_margin[i] + setup_adj[i];
		}else{
			setup_adj[i] = 0;
		}
		if (debug) {
			utils_dfi_trace(LOG_ERR, 0x77e6, "DFI CA leveling - Balanced CA%d setup margin is: 0x%x.\n", i, setup_margin[i]);
			utils_dfi_trace(LOG_ERR, 0x7ca0, "DFI CA leveling - Balanced CA%d hold margin is: 0x%x.\n", i, hold_margin[i]);
		}
	}

	if (skip_ca_sweep==0) {
		//---------------------------------------------------------------------
		// Write back
		//---------------------------------------------------------------------
		// Disable shadow-register simultaneous write
		sel_ctrl_1_t sel1     = { .all = dfi_readl(SEL_CTRL_1), };
		sel1.b.simul_oadly_ca = 0;
		dfi_writel(sel1.all,SEL_CTRL_1);
		for (int i=0; i<6; i++) {
			ctrl0.b.sel_ca = i;
			dfi_writel(ctrl0.all,SEL_CTRL_0);
			dfi_calvl_update_ca_wr(setup_adj[i]); //The default delay on CA is 0.
		}
		//Apply training results to target ranks
		dfi_calvl_update_ck_wr(CK_GATE_CS_ALL, hold_adj_max);
		dfi_calvl_update_rank_wr(CK_GATE_CS_ALL, hold_adj_max);
	}


	if (debug)
		utils_dfi_trace(LOG_ERR, 0x3046, "DFI CA leveling - Optimal VREF_CA is 0x%x @ range %d, CA eye size is %d.\n", vref_f, vref_range_start, eye_min_global);

	config2.b.fsp_op = 1; //switch to high frequency FSP
	config2.b.vrcg = 1; //high current mode required for frequency change
	mc0_writel(config2.all,DEVICE_CONFIG2);

	dfi_mc_mr_rw_req_seq(MR_WRITE, MR13, CS_ALL_BITS);

	//Config the trained VREF settings
	dfi_mc_vref_set(VREF_SET_CA, vref_range_start, vref_f, CS_ALL_BITS, debug);

	ndelay(1000);
	dfi_set_pll_freq(target_speed, true);//Change to high target speed.
	dfi_sync();
	ndelay(1000);
	config2.b.vrcg = 0; //get out of high current mode.
	mc0_writel(config2.all,DEVICE_CONFIG2);
	dfi_mc_mr_rw_req_seq(MR_WRITE, MR13, CS_ALL_BITS);

	// Apply enough +delay to CK/CA so they're longer than DQS at the start of write leveling
	dfi_push_adcm(ADCM_DLY_OFFSET);
	if (debug)
		utils_dfi_trace(LOG_ERR, 0x7076, "DFI CA leveling - Finished.\n");

	return 0;
}

/*!
 * @brief Switch to high speed after restoring registers in resuming from PS4
 *
 * @param target_speed	Set PLL to this speed
 *
 * @return	not used
 */
norm_ps_code void dfi_switch_fsp1(u16 target_speed)
{
	u8 cmd_cs_all = 0xf;

	mc_switch_latency(target_speed); // program CL, CWL, nWR
	device_config0_t config0 = { .all = mc0_readl(DEVICE_CONFIG0) };
	config0.b.wl_select = 0;
	mc0_writel(config0.all, DEVICE_CONFIG0);

	dfi_phy_cntl1_t cntl1  = { .all = mc0_readl(DFI_PHY_CNTL1), };
	cntl1.b.trddata_en = config0.b.cas_latency - 2; // more than 2 affects rdlvl_gate
	cntl1.b.tphy_wrlat = config0.b.cas_write_latency;
	mc0_writel(cntl1.all, DFI_PHY_CNTL1);

	// Config frequency set point 1
	device_config2_t config2 = { .all = mc0_readl(DEVICE_CONFIG2), };
	config2.b.fsp_wr = 1;
	mc0_writel(config2.all,DEVICE_CONFIG2);
	dfi_mc_mr_rw_req_seq(MR_WRITE, MR13, cmd_cs_all);	// Write to LP4 MR13, switch FSP_WR
	ndelay(100);
	dfi_mc_mr_rw_req_seq(MR_WRITE, MR1, cmd_cs_all);	// Write to LP4 MR1
	ndelay(100);
	dfi_mc_mr_rw_req_seq(MR_WRITE, MR2, cmd_cs_all);	// Write to LP4 MR2
	ndelay(100);
	dfi_mc_mr_rw_req_seq(MR_WRITE, MR3, cmd_cs_all);	// Write to LP4 MR3
	ndelay(100);
	dfi_mc_mr_rw_req_seq(MR_WRITE, MR11, cmd_cs_all);	// Write to LP4 MR11
	ndelay(100);
	dfi_mc_mr_rw_req_seq(MR_WRITE, MR14, cmd_cs_all);	// Write to LP4 MR14

	// VREF values are already restored with pmu_ddr_restore(), so just issue MRW
	dfi_mc_mr_rw_req_seq(MR_WRITE, MR12, cmd_cs_all); // VREF_CA

	config2.b.vrcg = 1;				// high current mode required for frequency change
	mc0_writel(config2.all,DEVICE_CONFIG2);

	dfi_mc_mr_rw_req_seq(MR_WRITE, MR13, cmd_cs_all);
	ndelay(1000);

	config2.b.fsp_op = 1;				// switch to high frequency FSP
	mc0_writel(config2.all,DEVICE_CONFIG2);

	dfi_mc_mr_rw_req_seq(MR_WRITE, MR13, cmd_cs_all);
	ndelay(1000);

	dfi_set_pll_freq(target_speed, true); // Change to high target speed.
	dfi_sync();
	ndelay(1000);

	config2.b.vrcg = 0; // Exit high current mode.
	mc0_writel(config2.all,DEVICE_CONFIG2);
	dfi_mc_mr_rw_req_seq(MR_WRITE, MR13, cmd_cs_all);

	//utils_dfi_trace(LOG_ERR, 0, "DFI goto FSP1 - Finished.\n");
	return;
}
/*! @} */
