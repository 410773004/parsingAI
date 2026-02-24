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

#ifndef _NAL_CMD_H_
#define _NAL_CMD_H_
#include "nand_cfg.h"

#define NAND_DEVICE_ID_MICRON			0x2C
#define NAND_DEVICE_ID_TOSHIBA			0x98
#define NAND_DEVICE_ID_HYNIX			0xAD
#define NAND_DEVICE_ID_SAMSUNG			0xEC
#define NAND_DEVICE_ID_SANDISK			0x45
#define NAND_DEVICE_ID_UNKNOWN			0xFF

enum nal_nand_cmd_t {
	/*! \brief Read Operation */
	NAND_READ_PAGE_CMD_CYC1			= 0x00,
	NAND_READ_PAGE_CMD_CYC2 		= 0x30,
	NAND_READ_PAGE_MP_CMD_CYC2		= 0x32,
	NAND_READ_PAGE_CACHE_SEQ_CMD_CYC1	= 0x31,
	NAND_READ_PAGE_CACHE_RDM_CMD_CYC1	= 0x00,
	NAND_READ_PAGE_CACHE_RDM_CMD_CYC2	= 0x31,
	NAND_READ_PAGE_CACHE_LST_CMD_CYC1	= 0x3F,
	NAND_READ_PAGE_SNAP_CMD_CYC2		= 0x20,
	NAND_READ_PAGE_SNAP_CMD_CYC2_4KB	= 0x50,///< Samsung TLC 4KB snap read

	/*! \brief Program Operation */
	NAND_PROGRAM_PAGE_CMD_CYC1		= 0x80,
	NAND_PROGRAM_PAGE_EXT_CMD_CYC1		= 0x81,
	NAND_PROGRAM_PAGE_CMD_SS_CONFIRM_CYC1	= 0x8B,///<  Samsung program confirm 1st CMD
	NAND_PROGRAM_PAGE_CMD_CYC2		= 0x10,
	NAND_PROGRAM_PAGE_MP_CMD_CYC2		= 0x11,
	NAND_PROGRAM_PAGE_CACHE_CMD_CYC2	= 0x15,
	NAND_PROGRAM_PAGE_CMD_SS_CYC2		= 0xC0,///<  Samsung program 2nd CMD
	NAND_PROGRAM_PAGE_TLC_CMD_CYC2		= 0x1A,
	NAND_PROGRAM_PAGE_SK_L_M_CMD_CYC2	= 0x22,///<  SK Hynix TLC program low & middle 2nd CMD
	NAND_PROGRAM_PAGE_SK_UPR_CMD_CYC2	= 0x23,///<  SK Hynix TLC program upper 2nd CMD

	/*! \brief Erase Operation */
	NAND_ERASE_BLOCK_CMD_CYC1		= 0x60,
	NAND_ERASE_BLOCK_CMD_CYC2		= 0xD0,
	NAND_ERASE_BLOCK_MP_CMD_CYC2		= 0xD1,

	/*! \brief Column Address Operation */
	NAND_CHANGE_READ_COLUMN_CMD_CYC1	= 0x05,
	NAND_CHANGE_READ_COLUMN_ENH_CMD_CYC1	= 0x06,
	NAND_CHANGE_WRITE_COLUMN_CMD_CYC1	= 0x85,
	NAND_CHANGE_READ_COLUMN_CMD_CYC2	= 0xE0,

	/*! \brief Read Status Command */
	NAND_READ_STATUS_70			= 0x70,
	NAND_READ_STATUS_71			= 0x71,
	NAND_READ_STATUS_73			= 0x73,
	NAND_READ_STATUS_78			= 0x78,
	NAND_READ_STATUS_F1			= 0xF1,
	NAND_READ_STATUS_F2			= 0xF2,
	NAND_READ_STATUS_F3			= 0xF3,
	NAND_READ_STATUS_F4			= 0xF4,

	/*! \brief Copyback Operation */
	NAND_COPYBACK_READ_CMD_CYC1		= 0x00,
	NAND_COPYBACK_READ_CMD_CYC2		= 0x35,
	NAND_COPYBACK_READ_MP_CMD_CYC2		= 0x32,
	NAND_COPYBACK_PROGRAM_CMD_CYC1		= 0x85,
	NAND_COPYBACK_PROGRAM_CMD_CYC2		= 0x10,
	NAND_COPYBACK_PROGRAM_MP_CMD_CYC2	= 0x11,

	/*! \brief Cache Operation */
	NAND_CACHE_PROGRAM_MP_1ST_CMD_CYC1_TG	= 0x80,
	NAND_CACHE_PROGRAM_MP_1ST_CMD_CYC2_TG	= 0x11,
	NAND_CACHE_PROGRAM_MP_2ND_CMD_CYC1_TG	= 0x81,
	NAND_CACHE_PROGRAM_MP_2ND_CMD_CYC2_TG	= 0x11,

	NAND_CACHE_PROGRAM_MP_1ST_CMD_CYC1_ONFI	= 0x85,
	NAND_CACHE_PROGRAM_MP_1ST_CMD_CYC2_ONFI	= 0x11,
	NAND_CACHE_PROGRAM_MP_2ND_CMD_CYC1_ONFI	= 0x85,
	NAND_CACHE_PROGRAM_MP_2ND_CMD_CYC2_ONFI	= 0x15,

	/*! \brief Misc Operation */
	NAND_READ_ID				= 0x90,
	NAND_READ_PARAM				= 0xEC,
	NAND_READ_UNIQUE_ID			= 0xED,
	NAND_GET_FEATURES			= 0xEE,
	NAND_SET_FEATURES			= 0xEF,
	NAND_DEV_RESET				= 0xFF,
	NAND_DEV_HARD_RESET			= 0xFD,
	NAND_DEV_SYNC_RESET			= 0xFC,
	NAND_DEV_LUN_RESET			= 0xFA,
	NAND_ZQ_CALIB_LONG			= 0xF9,
	NAND_ZQ_CALIB_SHORT			= 0xD9,

	/*! \brief Micron Operation */
	NAND_MU_GET_LUN_FEATURE			= 0xD4,
	NAND_MU_SET_LUN_FEATURE			= 0xD5,
	NAND_MU_VOLUME_SELECT			= 0xE1,
	NAND_MU_ODT_CONFIGURE			= 0xE2,
	NAND_MU_SLC_ENABLE			= 0xDA,
	NAND_MU_SLC_DISABLE			= 0xDF,
	NAND_MU_SF_SLC_ENABLE			= 0x91,
	NAND_MU_READ_RETRY			= 0x89,
	NAND_READ_PAGE_CMD_SBR_CYC2 		= 0x34,
	NAND_MU_READ_CALIB			= 0x96,
	NAND_MU_READ_WITH_SOFT			= 0x33,
	NAND_MU_READ_OUT_SOFT			= 0x36,

	/*! \brief TSB Operation */
	NAND_SLC_PREFIX				= 0xA2,
	NAND_LOW_PREFIX				= 0x01,
	NAND_MID_PREFIX				= 0x02,
	NAND_UPR_PREFIX				= 0x03,
	NAND_TOP_PREFIX				= 0x04,
	NAND_09_PREFIX				= 0x09, ///<  Some 3-step program TLC need this prefix
	NAND_0D_PREFIX				= 0x0D, ///< Toshiba QLC and some 3-step program TLC need this prefix
	NAND_07_PREFIX				= 0x07, ///< Toshiba BiCS4 TLC self-adjusting read prefix
	NAND_5D_PREFIX				= 0x5D, ///< Toshiba BiCS3 QLC read retry prefix
	NAND_36_PREFIX				= 0x36, ///< Toshiba BiCS4 fast read prefix
	NAND_26_PREFIX              = 0x26, ///< KIC BiCS5 LNA/DLA prefix
 	NAND_C2_PREFIX              = 0xC2, //Toshiba BiCS4 2BIT soft read cmd  //tony 20201208
	NAND_TSB_SBN_READ			= 0x3C,
	NAND_TSB_XNOR_CB			= 0xCB,
	NAND_TSB_XNOR_3F			= 0x3F,
	NAND_TSB_GET_TEMP_B9			= 0xB9, // Temperature readout: B9h-tBUSY-7Ch-DataOut
	NAND_TSB_GET_TEMP_7C			= 0x7C, // Temperature readout

	/*! \brief Sandisk Operation */
	NAND_SD_SF_RESET			= 0x89,
	NAND_CMD_WRITE_DQ_TRAINING_TX_READ	= 0X64,
};

enum nal_nand_addr_t {
	/*! \brief Read ID 90h Address, etc */
	NAND_ADDR_MANU_ID			= 0x00,
	NAND_ADDR_ONFI_ID			= 0x20,
	NAND_ADDR_JEDEC_ID			= 0x40,

	/*! \brief Read Parameter Address ECh */
	NAND_READ_PARAM_ADDR_ONFI		= 0x00,
	NAND_READ_PARAM_ADDR_TOGGLE		= 0x40,

	/*! \brief Set/Get Feature Address EFh/EEh */
	NAND_FA_ONFI_TIMING_MODE		= 0x01,
	NAND_FA_DDR_CONF			= 0x02,
	NAND_FA_DRIVER_STRENGTH			= 0x10,
	NAND_FA_MU_SLEEP_VCCQ			= 0xE6,
};

enum nal_nand_setting_t {
	/*! \brief Driver Strength Setting for ONFi or Toggle */
	NAND_ONFI_OUTPUT_DRIVER_STRENGTH_OD2	= 0x00,		///< 18 Ohm
	NAND_ONFI_OUTPUT_DRIVER_STRENGTH_OD1	= 0x01,		///< 25 Ohm
	NAND_ONFI_OUTPUT_DRIVER_STRENGTH_NM	= 0x02,		///< 35 Ohm
	NAND_ONFI_OUTPUT_DRIVER_STRENGTH_UD	= 0x03,		///< 50 Ohm

	NAND_TOGGLE_OUTPUT_DRIVER_STRENGTH_OD2	= 0x08,		///< Overdrive 2
	NAND_TOGGLE_OUTPUT_DRIVER_STRENGTH_OD1	= 0x06,		///< Overdrive 1
	NAND_TOGGLE_OUTPUT_DRIVRE_STRENGTH_NM	= 0x04,		///< Default
	NAND_TOGGLE_OUTPUT_DRIVER_STRENGTH_UD	= 0x02,		///< Underdrive

};

///  NAND read status bits
enum nal_nand_read_status {
	NAND_READ_STATUS_FAIL			= 0x01,
	NAND_READ_STATUS_FAILC			= 0x02,
	NAND_READ_STATUS_SUSPEND		= 0x04,
	NAND_READ_STATUS_ARDY			= 0x20,
	NAND_READ_STATUS_RDY			= 0x40,
	NAND_READ_STATUS_DIS_WP			= 0x80,
};


#define RDY_BIT_OFFSET	3
#define FAIL_BIT_OFFSET	0
#if STABLE_REGRESSION
#define RDY_BIT(x) (3 << (5 - RDY_BIT_OFFSET))
#else
#define RDY_BIT(x) (1 << (x - RDY_BIT_OFFSET))
#endif
#define FAIL_BIT(x) (1 << (x - FAIL_BIT_OFFSET))

#endif /* end _NAL_CMD_H_ */

