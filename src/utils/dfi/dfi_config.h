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
/*! \file dfi_config.h
 * @brief DFI (DDR PHY Interface) configuration description
 *
 * \addtogroup module_group
 * \defgroup module
 * \ingroup module_group
 * @{
 */
#pragma once
//-----------------------------------------------------------------------------
//  Include files:
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
//  Macros definitions:
//-----------------------------------------------------------------------------
#define CK_GATE_NORMAL		0
#define CK_GATE_CS0		0x1
#define CK_GATE_CS1		0x2
#define CK_GATE_CS2		0x4
#define CK_GATE_CS3		0x8
#define CK_GATE_CS_ALL		0xf

#define DFI_DDR3		2
#define DFI_DDR4		3
#define DFI_LPDDR4		11

//DFI init settings
//------------------Common settings for all DDR configurations-----------------
#define CGS_SEL             1 //PHY CG Start Delay Select, range from 0 to 3
#define WR_RSTB_SEL         3 //PHY Write Reset Delay Select, range from 0 to 3
#define WR_PT_INIT          1 //PHY WR_LD Initial Value, could be 1, 2, 4 and 8.
#define STRGT_MODE          3 //default single-ended DQS
#define STRGT_EN            0 //default disable strobe gate
//-------------------------------LPDDR4----------------------------------------
#if defined(LPDDR4)
	#define DFI_DRAM_TYPE       DFI_LPDDR4 //In DFI, DDR3=2, DDR4=3, LPDDR4=11
	#define DATA_PODT_SEL       0 //4'b0100, P side ODT, init to 120ohm
	#define DATA_NODT_SEL       0xc //LPDDR4 terminate to low

#if defined(M2)
	#define DATA_PDRV_SEL       0x8 //4'b1000, P side DRV, init to 60 ohm
	#define DATA_NDRV_SEL       0x8 //4'b1000, N side DRV, init to 60 ohm
#else
	#define DATA_PDRV_SEL       0xe //4'b1000, P side DRV, init to 60 ohm
	#define DATA_NDRV_SEL       0xe //4'b1000, N side DRV, init to 60 ohm
#endif

	#define ADCM_PDRV_SEL       0xf //dealt to max for ADCM
	#define ADCM_NDRV_SEL       0xf //dealt to max for ADCM
	#define CK_PDRV_SEL         0xf // CK coarse drive strength
	#define CK_NDRV_SEL         0xf // CK coarse drive strength
	#define ADCM_PSTR           0x17 //default strength
	#define ADCM_NSTR           0x17 //default strength
	#define DATA_PSTR           0x17 //default strength
	#define DATA_NSTR           0x17 //default strength
	#define DQS_PD				0
	#define DQS_PU				1
	#define DQSN_PD				0
	#define DQSN_PU				1
	#define DQ_PD				0
	#define DQ_PU				1
	#define ADCM_PD				0
	#define ADCM_PU				1
	#define OED_PH_DLY          6 //DQ/DM Output Phase Delay
	#define OEP_E_PH_DLY        7 //DQS Output E Phase Delay (OEP phase end delay)
	#define OEP_S_PH_DLY        3 //DQS Output S Phase Delay (OEP phase start delay)
	#define OEA_AUTO_ADDR       0  //Address OE automatic toggling for DDR3/4
	#define RDPTR_PRE_ADJ       0 //PHY Read FIFO pointer preamble adjust, TBM, 0: DDR3, DDR4, LPDDR4 (static preamble) 1: FPGA, LPDDR4 (toggle preamble)
	#define RDPTR_POST_ADJ      0 //PHY Read FIFO pointer postamble adjust, TBM, 0: DDR3, LPDDR4 (static preamble) 1: DDR4, LPDDR4 (toggle preamble)
	#define RDLAT               6 //RDLAT, could be updated by RDLAT Training
	#define RD_RSTB_OEPFE_DLY   3 //PHY Read FIFO Reset Delay - DQS Output triggered read reset
	#define RD_RSTB_DLY         0 //PHY Read FIFO Reset Delay - Receiver triggered read reset
	#define DATA_DIFF_EN        1 //Enable differential receiver
	#define DQS_RCV_SEL         0 //0: differential receiver, 1: CMOS receiver
	#define DQ_RCV_SEL          0 //0: differential receiver, 1: CMOS receiver
	#define DQS_RCV_MODE        1
	#define DQ_RCV_MODE         1
	#define DATA_MODE           0
	#define DATA_AUX_EN         1
	#define ECC_BYTE_REMAP      1
	//LPDDR4 specific settings
	#define WDQS_CONTROL_MODE2_ON_LP4   1 //LPDDR4, Enable WDQS_ON to asssert Write DQS before preamble
	#define WDQS_CONTROL_MODE1_LP4      1 //LPDDR4, Write DQS is expanded using read-based control
	#define WDQS_ON_LP4                 1 //LPDDR4, if WDQS_CONTROL_MODE2_ON is set, this is the value for WDQS_ON in units of 1tCK
	#define WDQS_OFF_LP4                1 //LPDDR4, if WDQS_CONTROL_MODE2_ON is set, this is the value for WDQS_OFF in units of 1tCK
	#if defined(M2_2A)
		#define ADCM_DLY_OFFSET     0
	#elif defined(M2_0305)
		#define ADCM_DLY_OFFSET     30
	#else
		#define ADCM_DLY_OFFSET     0
	#endif
	#define DATA_DLY_OFFSET     0
#else
//-------------------------------DDR4 (Default)--------------------------------
	#define DFI_DRAM_TYPE               DFI_DDR4 //In DFI, DDR3=2, DDR4=3, LPDDR4=11
	#if defined(M2)
		#define DATA_PODT_SEL       0x8 //P side ODT
		#define DATA_NODT_SEL       0x0 //N side ODT
		#define DATA_PDRV_SEL       0xc //P side DRV
		#define DATA_NDRV_SEL       0xc //N side DRV
		#define ADCM_PDRV_SEL       0x9 //P side DRV for ADCM
		#define ADCM_NDRV_SEL       0x9 //N side DRV for ADCM
		#define CK_PDRV_SEL         0x9 //P side DRV for CK
		#define CK_NDRV_SEL         0x9 //N side DRV for CK
		#define ADCM_DLY_OFFSET     30
		#define DATA_DLY_OFFSET     0
	#elif defined(U2)
		#define DATA_PODT_SEL       0x8 //P side ODT
		#define DATA_NODT_SEL       0x0 //N side ODT
		#define DATA_PDRV_SEL       0xc //P side DRV
		#define DATA_NDRV_SEL       0xc //N side DRV
		#define ADCM_PDRV_SEL       0x9 //P side DRV for ADCM
		#define ADCM_NDRV_SEL       0x9 //N side DRV for ADCM
		#define CK_PDRV_SEL         0x9 //P side DRV for CK
		#define CK_NDRV_SEL         0x9 //N side DRV for CK
		#define ADCM_DLY_OFFSET     10
		#define DATA_DLY_OFFSET     0
	#else // EVB
		#define DATA_PODT_SEL       4
		#define DATA_NODT_SEL       0
		#define DATA_PDRV_SEL       0xe
		#define DATA_NDRV_SEL       0xe
		#define ADCM_PDRV_SEL       0xF
		#define ADCM_NDRV_SEL       0xF
		#define CK_PDRV_SEL         0xF
		#define CK_NDRV_SEL         0xF
		#define ADCM_DLY_OFFSET     0
		#define DATA_DLY_OFFSET     30
	#endif
	#define ADCM_PSTR           0x17 //default strength
	#define ADCM_NSTR           0x17 //default strength
	#define DATA_PSTR           0x17 //default strength
	#define DATA_NSTR           0x17 //default strength
	#define DQS_PD				0
	#define DQS_PU				1
	#define DQSN_PD				0
	#define DQSN_PU				1
	#define DQ_PD				0
	#define DQ_PU				1
	#define ADCM_PD				0
	#define ADCM_PU				1
	#define OED_PH_DLY          2 //DQ/DM Output Phase Delay
	#define OEP_E_PH_DLY        3 //DQS Output E Phase Delay (OEP phase end delay)
	#define OEP_S_PH_DLY        1 //DQS Output S Phase Delay (OEP phase start delay)
	#define OEA_AUTO_ADDR       1  //Address OE automatic toggling for DDR3/4
	#define RDPTR_PRE_ADJ       0 //PHY Read FIFO pointer preamble adjust, TBM, 0: DDR3, DDR4, LPDDR4 (static preamble) 1: FPGA, LPDDR4 (toggle preamble)
	#define RDPTR_POST_ADJ      1 //PHY Read FIFO pointer postamble adjust, TBM, 0: DDR3, LPDDR4 (static preamble) 1: DDR4, LPDDR4 (toggle preamble)
	#define RDLAT               5 //RDLAT, could be updated by RDLAT Training
	#define RD_RSTB_OEPFE_DLY   3 //PHY Read FIFO Reset Delay - DQS Output triggered read reset
	#define RD_RSTB_DLY         0 //PHY Read FIFO Reset Delay - Receiver triggered read reset
	#define DATA_DIFF_EN        1 //Enable differential receiver
	#define DQS_RCV_SEL         0 //Choose differential receiver
	#define DQ_RCV_SEL          0 //Choose differential receiver
	#define DQS_RCV_MODE        2
	#define DQ_RCV_MODE         2
	#define DATA_MODE           1
	#define DATA_AUX_EN         1
	#define ECC_BYTE_REMAP      1
#endif

//-----------------------------------------------------------------------------
// PLL2 settings
//-----------------------------------------------------------------------------
#define DDR_PLL_3200 0x00488004
#define DDR_PLL_2666 0x00486A84
#define DDR_PLL_2400 0x00486004
#define DDR_PLL_2133 0x00485504
#define DDR_PLL_2000 0x00485004
#define DDR_PLL_1866 0x00509584
#define DDR_PLL_1600 0x00508004
#define DDR_PLL_1333 0x00506a84
#define DDR_PLL_1066 0x00505504
#define DDR_PLL_800  0x00608004
#define DDR_PLL_400  0x00A08004

//-----------------------------------------------------------------------------
//  Data type definitions: typedef, struct or class
//-----------------------------------------------------------------------------
/*!
 * @brief DFI Training Result Type
 */
enum {
	DFI_TRAIN_PASS 			= 0,		///< DFI training pass
	DFI_TRAIN_PAD_CAL_FAIL		= -1,		///< DFI training pad calibration fail
	DFI_TRAIN_CA_LVL_FAIL		= -2,		///< DFI training command level fail
	DFI_TRAIN_WR_LVL_FAIL 		= -3,		///< DFI training write level fail
	DFI_TRAIN_RD_LVL_RDLAT_FAIL  	= -4,		///< DFI training read level read latency fail
	DFI_TRAIN_RD_LVL_2D_FAIL  	= -5,		///< DFI training read level 2d fail
	DFI_TRAIN_RD_LVL_GATE_FAIL  	= -6,		///< DFI training read level gate fail
	DFI_TRAIN_WR_TRAIN_FAIL  	= -7,		///< DFI training write training fail
	DFI_TRAIN_VREF_DQ_FAIL  	= -8,		///< DFI training voltage reference dq fail
	DFI_TRAIN_WR_DQ_DQS_DLY_FAIL  	= -9,		///< DFI training write dq/dqs delay fail
	DFI_TRAIN_RD_LVL_DPE_FAIL  	= -10		///< DFI training read level dpe fail
};
//-----------------------------------------------------------------------------
//  Data declaration: Private or Public:
//-----------------------------------------------------------------------------

/*! @} */
