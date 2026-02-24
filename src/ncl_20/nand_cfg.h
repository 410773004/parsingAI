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

#if   HAVE_A0
#define SHASTA		1
#define SHASTAPLUS	0
#define RAINIER_A0	0
#define SHASTAPLUS_TEMP	1
#else
#define SHASTA		0
#define SHASTAPLUS	0
#define RAINIER_A0	1
#endif
#define SHASTAX (SHASTA || SHASTAPLUS)
#define TACOMAX		(RAINIER_A0 || RAINIER_HS || TACOMA12 || RAINIER_S)
#if FW_SUSPEND
#define NAND_FW_SUSPEND	1
#endif
#if RAINIER_A0 || SHASTAX
#endif
#ifndef _NAND_CFG_H_
#define _NAND_CFG_H_

#define META_SIZE		32

#ifndef MAX_CHANNEL
# define MAX_CHANNEL 8
#endif

#ifndef MAX_TARGET
# define MAX_TARGET  4 
#endif

#ifndef MAX_PLANE
# define MAX_PLANE  4
#endif

#ifndef MAX_DIE
#define MAX_DIE 128
#endif
#define MAX_LUN 4
/*! \brief NAND timing interface */
typedef enum {
	NDCU_IF_SDR		= 0x000,
	NDCU_IF_DDR		= 0x010,
	NDCU_IF_DDR2		= 0x020,
	NDCU_IF_DDR3		= 0x030,	///< Vcc 1.2V ONFI
	NDCU_IF_TGL1		= 0x040,
	NDCU_IF_TGL2		= 0x050,
} ndcu_if_mode;

// Nand ID address
#define NAND_MANU_ID_ADDR	0x00
#define NAND_ID_LENGTH		6

// Nand vendor ID
#define NAND_ID_TOSHIBA		0x98
#define NAND_ID_MICRON		0x2C
#define NAND_ID_UNIC		0x89
#define NAND_ID_YMTC		0x9B
#define NAND_ID_HYNIX		0xAD
#define NAND_ID_SANDISK		0x45
#define NAND_ID_SAMSUNG		0xEC

#if HAVE_HYNIX_SUPPORT
#define HAVE_TOGGLE_SUPPORT	1
#define NAND_ID_VENDOR		NAND_ID_HYNIX
#define DEFECT_MARK_USER	1
#define DEFECT_MARK_LAST_PAGE	1
#define NDCU_IF_DEFAULT		NDCU_IF_SDR
#define ROW_WL_ADDR			1
#define ERD_VENDOR		1
#endif

#if HAVE_TSB_SUPPORT
#define HAVE_TOGGLE_SUPPORT	1
#define NAND_ID_VENDOR		NAND_ID_TOSHIBA
#define DEFECT_MARK_USER	1
#define DEFECT_MARK_LAST_PAGE	1
#define NDCU_IF_DEFAULT		NDCU_IF_TGL2
#if defined(TSB_XL_NAND)
#define ROW_WL_ADDR                     0
#else
#define ROW_WL_ADDR			1
#endif
#define ERD_VENDOR		1
#if QLC_SUPPORT
#define MULTI_PROG_STEPS	1
#endif
//#define WARMUP_RD_CYCLES	4
//#define WARMUP_WR_CYCLES	4
#endif

#if HAVE_SANDISK_SUPPORT
#define HAVE_TOGGLE_SUPPORT	1
#define NAND_ID_VENDOR		NAND_ID_SANDISK
#define DEFECT_MARK_USER	1
#define DEFECT_MARK_LAST_PAGE	1
#define NDCU_IF_DEFAULT		NDCU_IF_SDR
#define ROW_WL_ADDR			1
#define ERD_VENDOR		1
#endif

#if HAVE_MICRON_SUPPORT
#define HAVE_ONFI_SUPPORT	1
#define NAND_ID_VENDOR		NAND_ID_MICRON
#define DEFECT_MARK_USER	0
#define DEFECT_MARK_LAST_PAGE	0
#define NDCU_IF_DEFAULT		NDCU_IF_SDR
#define ROW_WL_ADDR			0
#define DU_PADDING		0	/* DU padding to 1/4 page align*/
#define ERD_VENDOR		1
#define ERD_MU_SBSBR		1
#define ERD_MU_SOFT_READ	0
#if (NCL_HAVE_ERD) && ((ERD_MU_SBSBR + ERD_MU_SOFT_READ) > 1)
	#error "Can only enable 1 ERD"
#endif
//#define WARMUP_RD_CYCLES	4
//#define WARMUP_WR_CYCLES	4
#endif

#if HAVE_UNIC_SUPPORT
#define HAVE_ONFI_SUPPORT	1
#define NAND_ID_VENDOR		NAND_ID_UNIC
#define DEFECT_MARK_USER	0
#if XLC == 2
#define DEFECT_MARK_LAST_PAGE	0
#else
#define DEFECT_MARK_LAST_PAGE	0
#endif
#define NDCU_IF_DEFAULT		NDCU_IF_SDR
#define ROW_WL_ADDR			0
#define ERD_VENDOR		0
#define ERD_MU_SBSBR		0
#define ERD_MU_SOFT_READ	0 // Seems nand soft info wrong
#if (NCL_HAVE_ERD) && ((ERD_MU_SBSBR + ERD_MU_SOFT_READ) > 1)
	#error "Can only enable 1 ERD"
#endif
#endif

#if HAVE_YMTC_SUPPORT
#define HAVE_ONFI_SUPPORT	1
#define NAND_ID_VENDOR		NAND_ID_YMTC
#define DEFECT_MARK_USER	0
#define DEFECT_MARK_LAST_PAGE	0
#define NDCU_IF_DEFAULT		NDCU_IF_SDR
#define ROW_WL_ADDR			0
#define ERD_VENDOR		1
//#define WARMUP_RD_CYCLES	4
//#define WARMUP_WR_CYCLES	4
#endif

#if HAVE_SAMSUNG_SUPPORT
#define HAVE_TOGGLE_SUPPORT	1
#define NAND_ID_VENDOR		NAND_ID_SAMSUNG
#define DEFECT_MARK_USER	1
#define DEFECT_MARK_LAST_PAGE	0
#define NDCU_IF_DEFAULT		NDCU_IF_TGL1
#define ROW_WL_ADDR			0
#define ERD_VENDOR		0
#endif

#ifdef HAVE_VELOCE
#define NAND_ID_VELOCE		0xE0
#endif

#if QLC_SUPPORT
#define XLC			4
#elif TSB_XL_NAND
#define XLC			1	///< SLC only for TSB XL nand
#else
#define XLC			3	///< SLC ratio to XLC
#endif

/*! \brief The NAND interface enum */
enum nand_if_mode {
	UNKNOWN_NAND = 0,
	ONFI_NAND,
	TOGGLE_NAND,
};

extern u32 max_channel;
extern u32 max_target;
extern u32 max_lun;

#define STABLE_REGRESSION	0
#define DLL_PHASE_2_EDGE	0
#define ONFI_DCC_TRAINING 1
#if HAVE_TSB_SUPPORT || HAVE_MICRON_SUPPORT
#define WCNT_ENHANCE	1
#endif
#endif
