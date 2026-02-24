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
/*! \file ndphy.c
 * @brief nand PHY configure
 *
 * \addtogroup ncl
 * \defgroup ncl
 * \ingroup ncl
 * @{
 * Nand PHY configure APIs
 */
//=============================================================================

//-----------------------------------------------------------------------------
//  Include files:
//-----------------------------------------------------------------------------
#include "nand_cfg.h"
#include "ndphy.h"
#include "ndcu_reg_access.h"
#include "ncb_ndcu_register.h"
#include "ndcu.h"

/*! \cond PRIVATE */
#define __FILEID__ ndphy
#include "trace.h"
/*! \endcond */

/*!
 * @brief Set Nand PHY RE to DQS delay
 *
 * @param val_sync	Value of sync (toggle or ONFI DDR) mode
 * @param val_async	Value of async mode
 *
 * @return	not used
 */
norm_ps_code void ndphy_set_re2qs(u32 sync_val, u32 async_val)
{
	u8 ch;
	for (ch = 0; ch < max_channel; ch++) {
		nphy_ctrl_reg1_t r;
		r.all = ndphy_readl(ch, NPHY_CTRL_REG1);
		r.b.nphy_sync_re2qs_dly = sync_val;
		r.b.nphy_async_re2qs_dly = async_val;
                //_GENE_20200806  IG Suggestion
		r.b.nphy_vref_sel0 = 0;
		r.b.nphy_vref_sel1 = 1;
		r.b.nphy_vref_sel2 = 0;
		r.b.nphy_re2qs_gate_dly = 4;// Jamie
		ndphy_writel(ch, r.all, NPHY_CTRL_REG1);
	}

}

/*!
 * @brief Set Nand PHY interface timing mode
 *
 * @param ch	Channel
 * @param intf	Interface type
 *
 * @return	not used
 */
fast_code void ndphy_set_tm(u32 ch, ndcu_if_mode intf)
{
	u32 phy_reg;
	nphy_ctrl_reg0_t nphy_reg;

	switch(intf) {
	case NDCU_IF_SDR:
		phy_reg = ndphy_readl(ch, DTPHY_CTRL_REG0);
		phy_reg |= 0x00010000;	//  Async nand mode, using RE to sample
		ndphy_writel(ch, phy_reg, DTPHY_CTRL_REG0);

		phy_reg = ndphy_readl(ch, DTPHY_CTRL_REG1);
		phy_reg &= ~0x0000FF00;	//  Single data rate select
		ndphy_writel(ch, phy_reg, DTPHY_CTRL_REG1);

		break;
	case NDCU_IF_DDR:
		break;
	case NDCU_IF_DDR3:
	case NDCU_IF_DDR2:
	case NDCU_IF_TGL1:
	case NDCU_IF_TGL2:
		phy_reg = ndphy_readl(ch, DTPHY_CTRL_REG0);
		phy_reg &= ~0x00010000;	//  Sync nand mode, using DQS to sample
		ndphy_writel(ch, phy_reg, DTPHY_CTRL_REG0);

		phy_reg = ndphy_readl(ch, DTPHY_CTRL_REG1);
		phy_reg |= 0x0000FF00;	//  Double data rate select
		ndphy_writel(ch, phy_reg, DTPHY_CTRL_REG1);

		nphy_reg.all = ndphy_readl(ch, NPHY_CTRL_REG0);
		nphy_reg.b.nphy_sync_rdptr_dly = 2;
		ndphy_writel(ch, nphy_reg.all, NPHY_CTRL_REG0);
		break;
	default:
		break;
	}
}

/*!
 * @brief Nand PHY module reset
 *
 * @return	not used
 */
norm_ps_code void ndphy_hw_reset(void)
{
	u32 ch;
	nphy_ctrl_reg0_t reg;

	ncb_ndphy_trace(LOG_DEBUG, 0x2073, "ndphy hw reset");
	reg.all = ndphy_readl(0, NPHY_CTRL_REG0);
	for (ch = 0; ch < max_channel; ch++) {
		/*
		1 NDPHY Initialization
			1. Power on and before any NCB operation
			2. Wait for PLL output clock to be ready/stable.
			4. Program NPHY_SYNC_RSTN (nphy_ctrl_reg0, 0x000[0]) to be 0,
			and wait for at least 2 NF_CLK, and then program it back to 1
			5. Wait for 2 NF_CLK
			6. Program NPHY_SYNC_EN (nphy_ctrl_reg0, 0x000[1]) to be 1,
			and keep it high for 5 NF_CLK, and then program it back to 0
		*/

		reg.b.nphy_sync_rstn = 0;
		reg.b.nphy_sync_en = 0;
		reg.b.nphy_wrst_sel = 2;
		ndphy_writel(ch, reg.all, NPHY_CTRL_REG0);
		ndcu_delay(2);
		reg.b.nphy_sync_rstn = 1;
		ndphy_writel(ch, reg.all, NPHY_CTRL_REG0);
		ndcu_delay(2);
		reg.b.nphy_sync_en = 1;
		ndphy_writel(ch, reg.all, NPHY_CTRL_REG0);
		ndcu_delay(5);
		reg.b.nphy_sync_en = 0;
		ndphy_writel(ch, reg.all, NPHY_CTRL_REG0);
	}
}

/*!
 * @brief Nand PHY drive strength configure
 *
 * @param ch	Channel
 * @param level	Drive strength level
 *
 * @return	not used
 */
norm_ps_code void ndphy_drv_str_level(u32 ch, u32 level)
{
	dtphy_ctrl_reg5_t dtphy_reg5;

	dtphy_reg5.all = ndphy_readl(ch, DTPHY_CTRL_REG5);
	dtphy_reg5.b.dtphy_dstr3 = (level & BIT3) ? 0xFF : 0x00;
	dtphy_reg5.b.dtphy_dstr2 = (level & BIT2) ? 0xFF : 0x00;
	dtphy_reg5.b.dtphy_dstr1 = (level & BIT1) ? 0xFF : 0x00;
	dtphy_reg5.b.dtphy_dstr0 = (level & BIT0) ? 0xFF : 0x00;
	ndphy_writel(ch, dtphy_reg5.all, DTPHY_CTRL_REG5);
	ndphy_writel(ch, dtphy_reg5.all, CMDPHY_CTRL_REG5);
	ndphy_writel(ch, dtphy_reg5.all, CMDPHY_CTRL_REG20);

	dtphy_ctrl_reg14_t dtphy_reg14;

	dtphy_reg14.all = ndphy_readl(ch, DTPHY_CTRL_REG14);
	dtphy_reg14.all &= ~(0xF << DTPHY_QSP_DSTR0_SHIFT);
	dtphy_reg14.all &= ~(0xF << DTPHY_QSN_DSTR0_SHIFT);
	dtphy_reg14.all |= level << DTPHY_QSP_DSTR0_SHIFT;
	dtphy_reg14.all |= level << DTPHY_QSN_DSTR0_SHIFT;
	ndphy_writel(ch, dtphy_reg14.all, DTPHY_CTRL_REG14);

	cmdphy_ctrl_reg26_t cmdphy_reg26;
	cmdphy_reg26.all = ndphy_readl(ch, CMDPHY_CTRL_REG26);
	cmdphy_reg26.b.cmdphy_wp_dstr = level;
	ndphy_writel(ch, cmdphy_reg26.all, CMDPHY_CTRL_REG26);

	cmdphy_ctrl_reg14_t cmdphy_reg14;
	cmdphy_reg14.all = ndphy_readl(ch, CMDPHY_CTRL_REG14);
	cmdphy_reg14.all &= ~(0xF << CMDPHY_QSP_DSTR0_SHIFT);
	cmdphy_reg14.all &= ~(0xF << CMDPHY_QSN_DSTR0_SHIFT);
	cmdphy_reg14.all |= level << CMDPHY_QSP_DSTR0_SHIFT;
	cmdphy_reg14.all |= level << CMDPHY_QSN_DSTR0_SHIFT;
	ndphy_writel(ch, cmdphy_reg14.all, CMDPHY_CTRL_REG14);
}

/*!
 * @brief Nand PHY ODT configure
 *
 * @param ch	Channel
 * @param level	ODT level
 *
 * @return	not used
 */
init_code void ndphy_odt_level(u32 ch, u32 level)
{
	dtphy_ctrl_reg6_t dtphy_reg6;

	dtphy_reg6.all = ndphy_readl(ch, DTPHY_CTRL_REG6);
	dtphy_reg6.b.dtphy_odt_sel3 = (level & BIT3) ? 0xFF : 0x00;
	dtphy_reg6.b.dtphy_odt_sel2 = (level & BIT2) ? 0xFF : 0x00;
	dtphy_reg6.b.dtphy_odt_sel1 = (level & BIT1) ? 0xFF : 0x00;
	dtphy_reg6.b.dtphy_odt_sel0 = (level & BIT0) ? 0xFF : 0x00;
	ndphy_writel(ch, dtphy_reg6.all, DTPHY_CTRL_REG6);

	dtphy_ctrl_reg14_t dtphy_reg14;

	dtphy_reg14.all = ndphy_readl(ch, DTPHY_CTRL_REG14);
	dtphy_reg14.all &= ~(0xF << DTPHY_QSP_ODT_SEL0_SHIFT);
	dtphy_reg14.all &= ~(0xF << DTPHY_QSN_ODT_SEL0_SHIFT);
	dtphy_reg14.all |= level << DTPHY_QSP_ODT_SEL0_SHIFT;
	dtphy_reg14.all |= level << DTPHY_QSN_ODT_SEL0_SHIFT;
	ndphy_writel(ch, dtphy_reg14.all, DTPHY_CTRL_REG14);
}

/*!
 * @brief Nand PHY module initialization
 *
 * @return	not used
 */
norm_ps_code void ndphy_hw_init(void)
{
	u32 ch;
	nphy_ctrl_reg0_t reg0;
	dtphy_ctrl_reg1_t reg1;
	dtphy_ctrl_reg3_t reg3;
	dtphy_ctrl_reg13_t reg13;
	dtphy_ctrl_reg4_t reg4;

	for (ch = 0; ch < max_channel; ch++) {
		/*
		2	DLL Bypass Mode
			1.	DLL_BYPASS_EN = 1 (dtphy_ctrl_reg3, 0x014[9])
			2.	DLL_TST_DLY[8:0] = 0x30 (dtphy_ctrl_reg3, 0x014[8:0]),
				and DTPHY_RSVD_INT[1:0]= 1, (dtphy_ctrl_reg4, 0x018[13:12])
		*/
			reg3.all = ndphy_readl(ch, DTPHY_CTRL_REG3);
			reg3.b.dtphy_dll_bypass_en = 1;
			ndphy_writel(ch, reg3.all, DTPHY_CTRL_REG3);

			reg3.all = ndphy_readl(ch, DTPHY_CTRL_REG3);
			reg3.b.dtphy_dll_dly_test = 0x30;
			ndphy_writel(ch, reg3.all, DTPHY_CTRL_REG3);

			reg4.all = ndphy_readl(ch, DTPHY_CTRL_REG4);
			reg4.b.dtphy_rsvvd_int = (reg4.b.dtphy_rsvvd_int & 0xFC) | 0x1;
			ndphy_writel(ch, reg4.all, DTPHY_CTRL_REG4);

		/*
		3	CMOS Mode (DTPHY ONLY)
			1.	DTPHY_DQ_RCVTYPE= 0xFF (dtphy_ctrl_reg1, 0x00C[7:0])
			2.	DTPHY_QSP_RCVTYPE = 1 (dtphy_ctrl_reg13, 0x03C[12])
			3.	DTPHY_QSN_RCVTYPE = 1 (dtphy_ctrl_reg13, 0x03C[4])
		*/
			reg1.all = ndphy_readl(ch, DTPHY_CTRL_REG1);
			reg1.b.dtphy_dq_rcvtype = 0xFF;
			ndphy_writel(ch, reg1.all, DTPHY_CTRL_REG1);

			reg13.all = ndphy_readl(ch, DTPHY_CTRL_REG13);
			reg13.b.dtphy_qsp_rcvtype = 1;
			reg13.b.dtphy_qsn_rcvtype = 1;
			ndphy_writel(ch, reg13.all, DTPHY_CTRL_REG13);

		/*
		4	DQS Pull Up/Down (DTPHY ONLY)
			1.	DTPHY_QSP_PD = 1 (dtphy_ctrl_reg13, 0x03C[13])
			2.	DTPHY_QSN_PU = 1 (dtphy_ctrl_reg13, 0x03C[6])

		*/
			reg13.all = ndphy_readl(ch, DTPHY_CTRL_REG13);
			reg13.b.dtphy_qsp_pd = 1;
			reg13.b.dtphy_qsn_pu = 1;
			ndphy_writel(ch, reg13.all, DTPHY_CTRL_REG13);

		/*
		5 QS_GATE Disable (DTPHY ONLY)
			1. QS_GATE_DIS = 1, (nphy_ctrl_reg0, 0x000[18])
		*/
			reg0.all = ndphy_readl(ch, NPHY_CTRL_REG0);
			reg0.b.nphy_qs_gate_dis = 0; // Jamie
			ndphy_writel(ch, reg0.all, NPHY_CTRL_REG0);

		/*
		6 DSTR3/DSTR2/DSTR1/DSTR0 (both CMDPHY and DTPHY)
			1. DTPHY_DSTR3= 0xFF, (dtphy_ctrl_reg5, 0x01C[31:24])
			2. DTPHY_DSTR2= 0xFF, (dtphy_ctrl_reg5, 0x01C[23:16])
			3. DTPHY_DSTR1= 0x00, (dtphy_ctrl_reg5, 0x01C[15:8])
			4. DTPHY_DSTR0= 0x00, (dtphy_ctrl_reg5, 0x01C[7:0])
			5. DTPHY_QSP_DSTR3 = 1 (dtphy_ctrl_reg14, 0x040[31])
			6. DTPHY_QSP_DSTR2 = 1 (dtphy_ctrl_reg14, 0x040[30])
			7. DTPHY_QSP_DSTR1 = 0 (dtphy_ctrl_reg14, 0x040[29])
			8. DTPHY_QSP_DSTR0 = 0 (dtphy_ctrl_reg14, 0x040[28])
			9. DTPHY_QSN_DSTR3 = 1 (dtphy_ctrl_reg14, 0x040[15])
			10. DTPHY_QSN_DSTR2 = 1 (dtphy_ctrl_reg14, 0x040[14])
			11. DTPHY_QSN_DSTR1 = 0 (dtphy_ctrl_reg14, 0x040[13])
			12. DTPHY_QSN_DSTR0 = 0 (dtphy_ctrl_reg14, 0x040[12])
			13. CMDPHY_DSTR3= 0xFF, (cmdphy_ctrl_reg5, 0x01C[31:24])
			14. CMDPHY_DSTR2= 0xFF, (cmdphy_ctrl_reg5, 0x01C[23:16])
			15. CMDPHY_DSTR1= 0x00, (cmdphy_ctrl_reg5, 0x01C[15:8])
			16. CMDPHY_DSTR0= 0x00, (cmdphy_ctrl_reg5, 0x01C[7:0])
			17. CMDPHY_QSP_DSTR3 = 1 (cmdphy_ctrl_reg14, 0x040[31])
			18. CMDPHY_QSP_DSTR2 = 1 (cmdphy_ctrl_reg14, 0x040[30])
			19. CMDPHY_QSP_DSTR1 = 0 (cmdphy_ctrl_reg14, 0x040[29])
			20. CMDPHY_QSP_DSTR0 = 0 (cmdphy_ctrl_reg14, 0x040[28])
			21. CMDPHY_QSN_DSTR3 = 1 (cmdphy_ctrl_reg14, 0x040[15])
			22. CMDPHY_QSN_DSTR2 = 1 (cmdphy_ctrl_reg14, 0x040[14])
			23. CMDPHY_QSN_DSTR1 = 0 (cmdphy_ctrl_reg14, 0x040[13])
			24. CMDPHY_QSN_DSTR0 = 0 (cmdphy_ctrl_reg14, 0x040[12])
		*/
			dtphy_ctrl_reg5_t dtphy_reg5;

			dtphy_reg5.all = ndphy_readl(ch, DTPHY_CTRL_REG5);
			dtphy_reg5.b.dtphy_dstr3 = 0xFF;
			dtphy_reg5.b.dtphy_dstr2 = 0xFF;
			dtphy_reg5.b.dtphy_dstr1 = 0;
			dtphy_reg5.b.dtphy_dstr0 = 0;
			ndphy_writel(ch, dtphy_reg5.all, DTPHY_CTRL_REG5);

			dtphy_ctrl_reg14_t dtphy_reg14;

			dtphy_reg14.all = ndphy_readl(ch, DTPHY_CTRL_REG14);
			dtphy_reg14.b.dtphy_qsp_dstr3 = 1;
			dtphy_reg14.b.dtphy_qsp_dstr2 = 1;
			dtphy_reg14.b.dtphy_qsp_dstr1 = 0;
			dtphy_reg14.b.dtphy_qsp_dstr0 = 0;
			dtphy_reg14.b.dtphy_qsn_dstr3 = 1;
			dtphy_reg14.b.dtphy_qsn_dstr2 = 1;
			dtphy_reg14.b.dtphy_qsn_dstr1 = 0;
			dtphy_reg14.b.dtphy_qsn_dstr0 = 0;
			ndphy_writel(ch, dtphy_reg14.all, DTPHY_CTRL_REG14);

			cmdphy_ctrl_reg5_t cmdphy_reg5;

			cmdphy_reg5.all = ndphy_readl(ch, CMDPHY_CTRL_REG5);
			cmdphy_reg5.b.cmdphy_dstr3 = 0xFF;
			cmdphy_reg5.b.cmdphy_dstr2 = 0xFF;
			cmdphy_reg5.b.cmdphy_dstr1 = 0;
			cmdphy_reg5.b.cmdphy_dstr0 = 0;
			ndphy_writel(ch, cmdphy_reg5.all, CMDPHY_CTRL_REG5);
			cmdphy_ctrl_reg20_t cmdphy_reg20;
			cmdphy_reg20.all = ndphy_readl(ch, CMDPHY_CTRL_REG20);
			cmdphy_reg20.b.cmdphy_dstr3_d8 = 0x7;
			cmdphy_reg20.b.cmdphy_dstr2_d8 = 0x7;
			ndphy_writel(ch, cmdphy_reg20.all, CMDPHY_CTRL_REG20);

			cmdphy_ctrl_reg14_t cmdphy_reg14;
			cmdphy_reg14.all = ndphy_readl(ch, CMDPHY_CTRL_REG14);
			cmdphy_reg14.b.cmdphy_qsp_dstr3 = 1;
			cmdphy_reg14.b.cmdphy_qsp_dstr2 = 1;
			cmdphy_reg14.b.cmdphy_qsp_dstr1 = 0;
			cmdphy_reg14.b.cmdphy_qsp_dstr0 = 0;
			cmdphy_reg14.b.cmdphy_qsn_dstr3 = 1;
			cmdphy_reg14.b.cmdphy_qsn_dstr2 = 1;
			cmdphy_reg14.b.cmdphy_qsn_dstr1 = 0;
			cmdphy_reg14.b.cmdphy_qsn_dstr0 = 0;
			ndphy_writel(ch, cmdphy_reg14.all, CMDPHY_CTRL_REG14);

			if (ch == 0) {
				cmdphy_ctrl_reg26_t cmdphy_reg26;
				cmdphy_reg26.all = ndphy_readl(ch, CMDPHY_CTRL_REG26);
				cmdphy_reg26.b.cmdphy_wp_dstr = 0xF;
				ndphy_writel(ch, cmdphy_reg26.all, CMDPHY_CTRL_REG26);
			}
	dtphy_ctrl_reg13_t reg13;
	dtphy_ctrl_reg2_t reg2;
	cmdphy_ctrl_reg13_t creg13;
	cmdphy_ctrl_reg2_t creg2;

	// Configure both CMD_PHY and DATA_PHY to be working at 1.2V
	reg13.all = ndphy_readl(ch, DTPHY_CTRL_REG13);
	reg13.b.dtphy_qsp_ms = 0;
	reg13.b.dtphy_qsn_ms = 0;
	ndphy_writel(ch, reg13.all, DTPHY_CTRL_REG13);

	reg2.all = ndphy_readl(ch, DTPHY_CTRL_REG2);
	reg2.b.dtphy_dq_ms = 0x00;
	// Make data output 0xFF if no device
	reg2.b.dtphy_pd = 0x0;
	reg2.b.dtphy_pu = NO_DEV_SIGNAL;
	ndphy_writel(ch, reg2.all, DTPHY_CTRL_REG2);

	creg13.all = ndphy_readl(ch, CMDPHY_CTRL_REG13);
	creg13.b.cmdphy_qsp_ms = 0;
	creg13.b.cmdphy_qsn_ms = 0;
	ndphy_writel(ch, creg13.all, CMDPHY_CTRL_REG13);

	creg2.all = ndphy_readl(ch, CMDPHY_CTRL_REG2);
	creg2.b.cmdphy_dq_ms = 0x00;
	ndphy_writel(ch, creg2.all, CMDPHY_CTRL_REG2);

	}
	
#if 0 // Jamie 20200925 F_Calibration
	volatile nd_phy_calib_reg0_t reg = { .all = ndphy_readl(0, ND_PHY_CALIB_REG0) };
	reg.b.nd_cal_test &= 0x1;
	ndphy_writel(0, reg.all, ND_PHY_CALIB_REG0);
	// delay(1);

	reg.b.nd_cal_test |= 0x2;
	ndphy_writel(0, reg.all, ND_PHY_CALIB_REG0);
	//delay(1);

	reg.b.nd_cal_test &= 0x1;
	ndphy_writel(0, reg.all, ND_PHY_CALIB_REG0);
	//delay(1);

	reg.b.nd_cal_zq_en = 0x1;
	ndphy_writel(0, reg.all, ND_PHY_CALIB_REG0);

	do {
		reg.all = ndphy_readl(0, ND_PHY_CALIB_REG0);
	} while (false == reg.b.nd_cal_zq_done);

	reg.b.nd_cal_zq_en = 0x0;
	ndphy_writel(0, reg.all, ND_PHY_CALIB_REG0);

	for (ch = 0; ch < max_channel; ch ++) {
		dtphy_ctrl_reg4_t dst_reg = { .all = ndphy_readl(ch, DTPHY_CTRL_REG4) };
		dst_reg.b.dtphy_fc_trim0 = reg.b.nd_cal_zq_trim & 0x1;
		dst_reg.b.dtphy_fc_trim1 = (reg.b.nd_cal_zq_trim >> 1) & 0x1;
		ndphy_writel(ch, dst_reg.all, DTPHY_CTRL_REG4);
	}
#endif
}

/*!
 * @brief Nand PHY configure for 1.2V DDR3 mode
 *
 * @param ch	Channel
 *
 * @return	not used
 */
norm_ps_code void ndphy_init_ddr3(int ch)
{
	dtphy_ctrl_reg13_t reg13;
	dtphy_ctrl_reg2_t reg2;
	cmdphy_ctrl_reg13_t creg13;
	cmdphy_ctrl_reg2_t creg2;

	// Configure both CMD_PHY and DATA_PHY to be working at 1.2V
	reg13.all = ndphy_readl(ch, DTPHY_CTRL_REG13);
	reg13.b.dtphy_qsp_ms = 1;
	reg13.b.dtphy_qsn_ms = 1;
	ndphy_writel(ch, reg13.all, DTPHY_CTRL_REG13);

	reg2.all = ndphy_readl(ch, DTPHY_CTRL_REG2);
	reg2.b.dtphy_dq_ms = 0xFF;
	reg2.b.dtphy_pd = 0x0;
	reg2.b.dtphy_pu = NO_DEV_SIGNAL;
	ndphy_writel(ch, reg2.all, DTPHY_CTRL_REG2);

	creg13.all = ndphy_readl(ch, CMDPHY_CTRL_REG13);
	creg13.b.cmdphy_qsp_ms = 1;
	creg13.b.cmdphy_qsn_ms = 1;
	ndphy_writel(ch, creg13.all, CMDPHY_CTRL_REG13);

	if (gpio_io_voltage_is_1_2v()) {
		cmdphy_ctrl_reg19_t creg19;
		creg19.all = ndphy_readl(ch, CMDPHY_CTRL_REG19);
		creg19.b.cmdphy_dq_ms_d4 = 0x7;
		ndphy_writel(ch, creg19.all, CMDPHY_CTRL_REG19);
	}

	creg2.all = ndphy_readl(ch, CMDPHY_CTRL_REG2);
	creg2.b.cmdphy_dq_ms = 0xFF;
	ndphy_writel(ch, creg2.all, CMDPHY_CTRL_REG2);
}

/*!
 * @brief Nand PHY DLL phase update
 *
 * @param ch	Channel
 *
 * @return	not used
 */
norm_ps_code void ndphy_dll_update(int ch)
{
	dtphy_ctrl_reg3_t reg3;

	reg3.all = ndphy_readl(ch, DTPHY_CTRL_REG3);
	reg3.b.dtphy_dll_update_en = 1;
	ndphy_writel(ch, reg3.all, DTPHY_CTRL_REG3);
	ndcu_delay(2);
	reg3.b.dtphy_dll_update_en = 0;
	ndphy_writel(ch, reg3.all, DTPHY_CTRL_REG3);
}
extern u32 nand_phy_ch(u32 log_ch);
extern u8 nand_channel_num(void);
norm_ps_code void ndphy_io_strobe_enable(bool enable)
{
	nphy_ctrl_reg0_t reg1;
	u32 ch = 0, phy_ch = 0;
	nf_dll_updt_ctrl_reg0_t reg0;

	reg0.all = ndcu_readl(NF_DLL_UPDT_CTRL_REG0);
	if (enable) {
		reg0.b.nf_dll_updt_en = 0x3;
	} else {
		reg0.b.nf_dll_updt_en = 0;
	}
	ndcu_writel(reg0.all, NF_DLL_UPDT_CTRL_REG0);

	for (ch = 0; ch < nand_channel_num(); ch++) {
		phy_ch = nand_phy_ch(ch);
		reg1.all = ndphy_readl(phy_ch, NPHY_CTRL_REG0);
		reg1.b.nphy_io_strb_en = enable;
		ndphy_writel(phy_ch, reg1.all, NPHY_CTRL_REG0);
	}
}
/*!
 * @brief Nand PHY DLL phase lock
 *
 * @param ch	Channel
 *
 * @return	not used
 */
norm_ps_code void ndphy_init_per_ch_dll_lock(int ch)
{
	nphy_ctrl_reg0_t reg;
	dtphy_ctrl_reg3_t reg3;
	int i;

	reg.all = ndphy_readl(ch, NPHY_CTRL_REG0);
	reg.b.nphy_sync_rstn = 0;
	ndphy_writel(ch, reg.all, NPHY_CTRL_REG0);

	reg.all = ndphy_readl(ch, NPHY_CTRL_REG0);
	reg.b.nphy_sync_rstn = 1;
	ndphy_writel(ch, reg.all, NPHY_CTRL_REG0);

	reg.all = ndphy_readl(ch, NPHY_CTRL_REG0);
	reg.b.nphy_sync_en = 1;
	ndphy_writel(ch, reg.all, NPHY_CTRL_REG0);
	ndcu_delay(1);
	reg.all = ndphy_readl(ch, NPHY_CTRL_REG0);
	reg.b.nphy_sync_en = 0;
	ndphy_writel(ch, reg.all, NPHY_CTRL_REG0);

	extern bool nand_is_1p2v(void);
	if (nand_is_1p2v())
		ndphy_init_ddr3(ch);

	//  DLL Init
	reg3.all = ndphy_readl(ch, DTPHY_CTRL_REG3);
	reg3.b.dtphy_dll_bypass_en = 0;
	ndphy_writel(ch, reg3.all, DTPHY_CTRL_REG3);
	reg3.all = ndphy_readl(ch, DTPHY_CTRL_REG3);
	reg3.b.dtphy_dll_rstb = 1;
	ndphy_writel(ch, reg3.all, DTPHY_CTRL_REG3);

	reg3.all = ndphy_readl(ch, DTPHY_CTRL_REG3);
	reg3.b.dtphy_dll_rstb = 0;
	ndphy_writel(ch, reg3.all, DTPHY_CTRL_REG3);

	reg3.all = ndphy_readl(ch, DTPHY_CTRL_REG3);
	reg3.b.dtphy_dll_rstb = 1;
	ndphy_writel(ch, reg3.all, DTPHY_CTRL_REG3);

	ndphy_dll_update(ch);

#if !defined(HAVE_SYSTEMC)
	i = 0;
	do {
		reg3.all = ndphy_readl(ch, DTPHY_CTRL_REG3);
		++i;
	} while (!reg3.b.dtphy_dll_locked && (i < 1000));

	if (!reg3.b.dtphy_dll_locked)
		ncl_console_trace(LOG_ERR, 0xd5a5, "CH %d PHY DLL Lock failed", ch);
#endif

}

/*!
 * @brief Set Nand PHY DLL phase
 *
 * @param ch	Channel
 * @param value	DLL phase
 *
 * @return	not used
 */
norm_ps_code void ndphy_set_dll_phase(u8 ch, u8 value)
{
	dtphy_ctrl_reg3_t reg3;

	reg3.all = ndphy_readl(ch, DTPHY_CTRL_REG3);
	reg3.b.dtphy_dll_phsel0 = value;
	ndphy_writel(ch, reg3.all, DTPHY_CTRL_REG3);

	ndphy_dll_update(ch);

	return;
}
norm_ps_code void ndphy_set_dll_phase_enhance(u8 ch, u8 ce, u8 lun, u8 value)
{
	dtphy_dll_cfg_ptr_t ptr;
	dtphy_dll_phase_t phase;

	ptr.all = ndphy_readl(ch, DTPHY_DLL_CFG_PTR);
	ptr.b.dtphy_dll_cfg_ptr = ce + lun * 8;
	ndphy_writel(ch, ptr.all, DTPHY_DLL_CFG_PTR);
	phase.all = ndphy_readl(ch, DTPHY_DLL_PHASE);
	phase.b.dtphy_dll_phase = value;
	ndphy_writel(ch, phase.all, DTPHY_DLL_PHASE);
}

norm_ps_code void ndphy_set_re2qs_gate_dly(u32 delay)  //Sean_HS_220915
{
	u8 ch;
	nphy_ctrl_reg0_t reg0;
	for (ch = 0; ch < MAX_CHANNEL; ch++) {
		nphy_ctrl_reg1_t r;

		reg0.all = ndphy_readl(ch, NPHY_CTRL_REG0);
		reg0.b.nphy_qs_gate_dis = 0;
		ndphy_writel(ch, reg0.all, NPHY_CTRL_REG0);


		r.all = ndphy_readl(ch, NPHY_CTRL_REG1);
		r.b.nphy_re2qs_gate_dly = delay;
		ndphy_writel(ch, r.all, NPHY_CTRL_REG1);
	}
}

norm_ps_code void ndphy_set_re2qs_dly(u32 sync_val)
{
	u8 ch;
	for (ch = 0; ch < MAX_CHANNEL; ch++) {
		nphy_ctrl_reg1_t r;
		r.all = ndphy_readl(ch, NPHY_CTRL_REG1);
		r.b.nphy_sync_re2qs_dly = sync_val;
		ndphy_writel(ch, r.all, NPHY_CTRL_REG1);
	}
}

norm_ps_code void ndphy_set_qs_gate_dis(u8 dis)
{
	u8 ch;
	nphy_ctrl_reg0_t reg0;
	for (ch = 0; ch < MAX_CHANNEL; ch++) {
		reg0.all = ndphy_readl(ch, NPHY_CTRL_REG0);
		reg0.b.nphy_qs_gate_dis = dis;
		ndphy_writel(ch, reg0.all, NPHY_CTRL_REG0);

	}
}

/*!
 * @brief Nand PHY lock after timing mode switched
 *
 * @return	not used
 */
norm_ps_code void ndphy_locking_mode(void)
{
	int ch;
	u8 default_dll_phase = 28;

	for (ch = 0; ch < max_channel; ch++) {
		dtphy_ctrl_reg1_t reg1;
		dtphy_ctrl_reg3_t reg3;
		dtphy_ctrl_reg13_t reg13;

		reg1.all = ndphy_readl(ch, DTPHY_CTRL_REG1);
		reg1.b.dtphy_dq_rcvtype = 0x00;
		ndphy_writel(ch, reg1.all, DTPHY_CTRL_REG1);

		reg13.all = ndphy_readl(ch, DTPHY_CTRL_REG13);
		reg13.b.dtphy_qsp_rcvtype = 0;
		reg13.b.dtphy_qsn_rcvtype = 0;
		ndphy_writel(ch, reg13.all, DTPHY_CTRL_REG13);

#if 0
	srb_t *srb = (srb_t *) SRB_HD_ADDR;
		if (srb->srb_hdr.srb_signature == SRB_SIGNATURE) {
			if (srb->setnanddllenabled) {
				default_dll_phase = srb->nanddllvalues[ch];
			}
		}
#endif
		reg3.all = ndphy_readl(ch, DTPHY_CTRL_REG3);
		reg3.b.dtphy_dll_bypass_en = 0;
		reg3.b.dtphy_dll_phsel0 = default_dll_phase;
		ndphy_writel(ch, reg3.all, DTPHY_CTRL_REG3);

		ndphy_init_per_ch_dll_lock(ch);
	}

	return;
}

/*!
 * @brief Nand PHY differential mode enable/disable
 *
 * @param en	enable
 *
 * @return	not used
 */
norm_ps_code void ndphy_set_differential_mode(bool en)
{
	int ch;
	cmdphy_wp_ctrl_reg_t reg;
	nphy_ctrl_reg0_t r0;
	dtphy_ctrl_reg4_t r4;

	for (ch = 0; ch < max_channel; ch++) {
		reg.all = ndphy_readl(ch, CMDPHY_WP_CTRL_REG);
		reg.b.cmdphy_diff_en = en;
		ndphy_writel(ch, reg.all, CMDPHY_WP_CTRL_REG);

		r0.all = ndphy_readl(ch, NPHY_CTRL_REG0);
		r0.b.nphy_diff_en = en;
		ndphy_writel(ch, r0.all, NPHY_CTRL_REG0);

		r4.all = ndphy_readl(ch, DTPHY_CTRL_REG4);
		r4.b.dtphy_qs_diff_en = en;
		ndphy_writel(ch, r4.all, DTPHY_CTRL_REG4);
	}
}

/*!
 * @brief Nand PHY ODT enable/disable
 *
 * @param en	enable
 *
 * @return	not used
 */
norm_ps_code void ndphy_enable_odt(bool en)
{
	int ch;
	for (ch = 0; ch < max_channel; ch++) {
		dtphy_ctrl_reg4_t dtphy_ctrl_reg4;
		dtphy_ctrl_reg4.all = ndphy_readl(ch, DTPHY_CTRL_REG4);
		dtphy_ctrl_reg4.b.dtphy_fc_odt_en = en;
		ndphy_writel(ch, dtphy_ctrl_reg4.all, DTPHY_CTRL_REG4);
	}
}

/*!
 * @brief Nand PHY reset
 *
 * @return	not used
 */
norm_ps_code void ndphy_reset(void)
{
	u32 ch;
	nphy_ctrl_reg0_t reg;

	ncb_ndphy_trace(LOG_INFO, 0x825e, "ndphy_reset");
	for (ch = 0; ch < max_channel; ch++) {

		reg.all = ndphy_readl(ch, NPHY_CTRL_REG0);
		//ncb_ndphy_trace(LOG_INFO, 0, "1. NPHY_CTRL_REG0 : 0x%x",reg.all);
		reg.b.nphy_sync_rstn = 0;
 		reg.b.nphy_sync_en = 0;
 		reg.b.nphy_wrst_sel = 2;
		reg.b.nphy_dqs_cnt_rst = 0;
		reg.b.nphy_rd_ptr_rst = 0;
		ndphy_writel(ch, reg.all, NPHY_CTRL_REG0);
		ndcu_delay(2);
 		reg.b.nphy_sync_rstn = 1;
 		ndphy_writel(ch, reg.all, NPHY_CTRL_REG0);
 		ndcu_delay(2);
		reg.b.nphy_sync_en = 1;
		reg.b.nphy_dqs_cnt_rst = 1;
		reg.b.nphy_rd_ptr_rst = 1;
		ndphy_writel(ch, reg.all, NPHY_CTRL_REG0);
		ndcu_delay(5);
 		reg.b.nphy_sync_en = 0;
		reg.b.nphy_dqs_cnt_rst = 0;
		reg.b.nphy_rd_ptr_rst = 0;
		ndphy_writel(ch, reg.all, NPHY_CTRL_REG0);
		reg.all = ndphy_readl(ch, NPHY_CTRL_REG0);
		//ncb_ndphy_trace(LOG_INFO, 0, "2. NPHY_CTRL_REG0 : 0x%x",reg.all);
	}
}
/*! @} */
