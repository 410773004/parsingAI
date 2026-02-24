//-----------------------------------------------------------------------------
//		Copyright(c) 2016-2020 Innogrit Corporation
//			  All Rights reserved.
//
// The confidential and proprietary information contained in this file may
// only be used by a person authorized under and to the extent permitted
// by a subsisting licensing agreement from Innogrit Corporation.
// Dissemination of this information or reproduction of this material
// is strictly forbidden unless prior written permission is obtained
// from Innogrit Corporation.
//-----------------------------------------------------------------------------
/*! \file ddr_info.h
 * @brief ddr information header
 *
 * \addtogroup rtos
 * \defgroup ddr_info
 * \ingroup rtos
 * @{
 */

#pragma once
//-----------------------------------------------------------------------------
//  Include files:
//-----------------------------------------------------------------------------
#if defined(LOADER)
#include "../utils/dfi/dfi_reg.h"
#include "../rtos/armv7r/mc_reg.h"
#else
#include "types.h"
#include "dfi_reg.h"
#include "mc_reg.h"
#endif
//-----------------------------------------------------------------------------
//  Macros definitions:
//-----------------------------------------------------------------------------
#define HW_MAX_TUNE_STEPS	64
#ifdef ENABLE_PARALLEL_ECC
    #define DDR_MAX_BYTE		5
#else
    #define DDR_MAX_BYTE		4
#endif

#define DDR_MAX_BIT		(DDR_MAX_BYTE * 8)
#define DDR_MAX_CS		2
#define DDR_MAX_CA_NUM		12

//-----------------------------------------------------------------------------
//  Data type definitions: typedef, struct or class
//-----------------------------------------------------------------------------
// @brief DDR DFI data bit register backup
typedef struct _dfi_dbit_bkup_t {
	u8 sel_rd_dly_1;	///< SEL_RD_DLY_1's sel_rd_dly_1
	u8 dq_wr_dly;		///< SEL_ODDLY_2's dq_wr_dly
} dfi_dbit_bkup_t;
BUILD_BUG_ON(sizeof(dfi_dbit_bkup_t) != 2);

// @brief DDR DFI data byte register backup
typedef struct _dfi_dbyte_bkup_t {
	u8 dll_phase0;		///< SEL_DLL_0's dll_phase0
	u8 dll_phase1;		///< SEL_DLL_0's dll_phase1
	u8 dm_rd_dly;		///< SEL_RD_DLY_0's dm_rd_dly
	u8 strgt_tap_dly;	///< SEL_STRGT_0's strgt_tap_dly
	u8 strgt_phase_dly;	///< SEL_STRGT_0's strgt_phase_dly
	u8 dqs_wr_dly;		///< SEL_ODDLY_0's dqs_wr_dly
	u8 dm_wr_dly;		///< SEL_ODDLY_1's dm_wr_dly
	u8 ck_wr_dly;		///< SEL_OADLY_1's ck_wr_dly
	u8 rank_wr_dly;		///< SEL_OADLY_2's rank_wr_dly
	u8 rank_wr_rsv;		///< SEL_OADLY_2's rank_wr_rsv
} dfi_dbyte_bkup_t;
BUILD_BUG_ON(sizeof(dfi_dbyte_bkup_t) != 10);

// @brief DDR DFI Command Address register backup
typedef struct _dfi_ca_bkup_t {
	u8 ca_wr_dly;		///< consider LPDDR4 may with 2*6bits ca bus
} dfi_ca_bkup_t;
BUILD_BUG_ON(sizeof(dfi_ca_bkup_t) != 1);

// @brief DDR DFI general register backup
typedef struct _dfi_bkup_t {
	io_data_0_t data0;		///< IO_DATA_0
	io_data_1_t data1;		///< IO_DATA_1
	io_data_2_t data2;		///< IO_DATA_2
	io_adcm_0_t adcm0;		///< IO_ADCM_0
	io_adcm_1_t adcm1;		///< IO_ADCM_1
	io_adcm_2_t adcm2;		///< IO_ADCM_2
	io_adcm_3_t adcm3;		///< IO_ADCM_3
	io_ck_0_t ck0;			///< IO_CK_0
	io_alertn_t alert;		///< IO_ALERTN
	io_resetn_t resetn;		///< IO_RESETN
	io_cal_0_t cal0;		///< IO_CAL_0
	io_cal_1_t cal1;		///< IO_CAL_1
	vgen_ctrl_0_t vgen_ctrl0;	///< VGEN_CTRL_0
	sync_ctrl_0_t sync_ctrl;	///< SYNC_CTRL_0
	dfi_ctrl_0_t dfi_ctrl;		///< DFI_CTRL_0
	out_ctrl_0_t out_ctrl0;		///< OUT_CTRL_0
	out_ctrl_1_t out_ctrl1;		///< OUT_CTRL_1
	in_ctrl_0_t in_ctrl0;		///< IN_CTRL_0
	ck_gate_0_t ck_gate;		///< CK_GATE_0
	lvl_all_ctrl_0_t lvl_all_ctrl0;	///< LVL_ALL_CTRL_0
	dll_ctrl_1_t dll_ctrl1;		///< DLL_CTRL_1
	strgt_ctrl_0_t strgt_ctrl0;	///< STRGT_CTRL_0
	oadly_0_t oadly0;		///< OADLY_0
	lp_ctrl_0_t lp_ctrl0;		///< LP_CTRL_0
	dfi_dbit_bkup_t dfi_dbit_bkup[DDR_MAX_BIT];		//64
	dfi_dbyte_bkup_t dfi_dyte_bkup[DDR_MAX_BYTE];		//40
	dfi_ca_bkup_t dfi_ca_bkup[DDR_MAX_CA_NUM];		//12
} dfi_bkup_t;
#ifdef ENABLE_PARALLEL_ECC
BUILD_BUG_ON(sizeof(dfi_bkup_t) != (96+80+50+2+12));
#else
BUILD_BUG_ON(sizeof(dfi_bkup_t) != (96+64+40+12));
#endif

typedef struct _mc_bkup_t {
	device_config_training_t dev_cfg_training;	///< DEVICE_CONFIG_TRAINING
	dram_timing_offspec_t dram_timing_offspec;	///< DRAM_TIMING_OFFSPEC
	dfi_phy_cntl1_t dfi_phy_cntl1;			///< DFI_PHY_CNTL1
	dfi_phy_cntl3_t dfi_phy_cntl3;			///< DFI_PHY_CNTL3
	interrupt_cntl0_t interrupt_cntl0;		///< INTERRUPT_CNTL0
} mc_bkup_t;
BUILD_BUG_ON(sizeof(mc_bkup_t) != 20);

// @brief DDR Info of Type
typedef enum ddr_info_type {
	DDR_INFO_DDR4 = 0,
	DDR_INFO_LPDDR4,
	DDR_INFO_DDR3,
	DDR_INFO_LPDDR3,
	NUM_DDR_INFO_TYPE
} ddr_info_type;

// @brief DDR Info of Type
typedef enum ddr_info_vendor {
	DDR_INFO_MICRON = 0,
	DDR_INFO_HYNIX,
	DDR_INFO_SAMSUNG,
	NUM_DDR_INFO_VENDOR
} ddr_info_vendor;

// @brief DDR Configuration Data Structure
typedef struct _ddr_cfg_t {
	u8 type:4;		///< DDR3, DDR4, LPDDR3, LPDDR4
	u8 vendor:4;		///< MICRON, HYNIX, SUNSNG...
	u8 training_done:1;	///< ddr training done
	u8 bkup_fwconfig_done:1;///< ddr bkup done
	u8 ecc:2;		///< ddr ecc type, none:0, inline:1, pecc:2 if
	u8 speed:4;		///< ddr speed
	u8 cs;			///< cs number of ddr
	u8 bank;		///< bank bit cnt, 1 base
	u8 bank_gp;		///< bank group bit cnt, 1 base
	u8 row;			///< row bit cnt, 1 base
	u8 col;			///< column bit cnt, 1 base
	u8 bus_width;		///< bus width bit cnt, e.g. 16Bits: 4, 32Bits: 5
	u8 size;		///< size of ddr
	u8 reserved[7];		///< reserved
} ddr_cfg_t;
BUILD_BUG_ON(sizeof(ddr_cfg_t) != 16);

typedef struct _ddr_info_t {
	ddr_cfg_t cfg;						//16
	dfi_bkup_t dfi_bkup;					//96+64+40+12
	mc_bkup_t mc_bkup;					//20
	//#ifdef ENABLE_PARALLEL_ECC
	    //u8 reserved[44];					//44
	    u8 reserved[71];                    //40
	    u8 info_need_update;               //4
	//#else
	 //   u8 reserved[72];					//72
	//#endif
} ddr_info_t;
BUILD_BUG_ON(sizeof(ddr_info_t) != 320);

//-----------------------------------------------------------------------------
//  Data declaration: Private or Public:
//-----------------------------------------------------------------------------

/*! @} */

