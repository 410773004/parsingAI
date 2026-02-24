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

#pragma once

enum {	///< Common nand command format of all nands
	FINST_NAND_FMT_AUTO		= 0x0,		///< FW not used
	FINST_NAND_FMT_TLC_ERASE,			///< XLC block erase
	FINST_NAND_FMT_SLC_ERASE,			///< SLC block erase
	FINST_NAND_FMT_SLC_PROG,			///< SLC page program
	FINST_NAND_FMT_SLC_CACHE_PROG,			///< SLC page cache program
	FINST_NAND_FMT_SLC_PROG_FOR_RANDOM,		///< 5 SLC 80h program w/o 10h confirm, work together with next FINST_NAND_FMT_SLC_RANDOM_PROG
	FINST_NAND_FMT_SLC_RANDOM_PROG,			///< 85h-10h, work together with previous FINST_NAND_FMT_SLC_PROG_FOR_RANDOM
	FINST_NAND_FMT_SLC_READ_CMD,			///< SLC page read
	FINST_NAND_FMT_READ_DATA,			///< Random data out from page register
	FINST_NAND_FMT_SLC_CACHE_READ_CMD,		///< SLC page cache read
	FINST_NAND_FMT_CACHE_READ_END_CMD,		///< 10 cache read end
	FINST_NAND_FMT_SLC_CB_READ_CMD,			///< SLC page copyback read
	FINST_NAND_FMT_SLC_4K_READ,			///< SLC page 4KB snap/fast read
	FINST_NAND_FMT_READ_STATUS,			///< 70h read status
	FINST_NAND_FMT_READ_STATUS_ENH,			///< 78h read status enhanced
	FINST_NAND_FMT_READ_STATUS_FN,			///< 15 Fnh read LUN status
	FINST_NAND_FMT_FEATURE,				///< Set/Get feature
	FINST_NAND_FMT_FEATURE_LUN,			///< Set/Get feature by LUN
	FINST_NAND_FMT_RESET,				///< FFh reset
	FINST_NAND_FMT_READ_PARAM_PAGE,			///< Read parameter page
	FINST_NAND_FMT_COMMON_MAX,			///< Other vendor specific nand command format start from this
};
