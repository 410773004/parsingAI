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

#include "ndcmd_fmt_common.h"

// Toshiba specific nand command formats
enum {
	FINST_NAND_FMT_XLC_READ_LOW = FINST_NAND_FMT_COMMON_MAX,///< 20 XLC read low page
	FINST_NAND_FMT_XLC_READ_MID,			///< XLC read middle page
	FINST_NAND_FMT_XLC_READ_UPR,			///< XLC read upper page
	FINST_NAND_FMT_XLC_READ_TOP,			///< 23 XLC read top page
	FINST_NAND_FMT_XLC_CACHE_READ_LOW,		///< XLC cache read low page
	FINST_NAND_FMT_XLC_CACHE_READ_MID,		///< XLC cache read middle page
	FINST_NAND_FMT_XLC_CACHE_READ_UPR,		///< XLC cache read upper page
	FINST_NAND_FMT_XLC_CACHE_READ_TOP,		///< XLC cache read top page
	FINST_NAND_FMT_XLC_PROG_LOW,			///< 28 XLC program low page
	FINST_NAND_FMT_XLC_PROG_MID,			///< XLC program middle page
	FINST_NAND_FMT_XLC_PROG_UPR,			///< XLC program upper page
	FINST_NAND_FMT_XLC_PROG_TOP,			///< XLC program top page
	FINST_NAND_FMT_XLC_CACHE_PROG_UPR,		///< TLC cache program upper page
	FINST_NAND_FMT_XLC_PROG_LOW_1ST,		///< 33 QLC program low page 1st (each WL is programmed twice)
	FINST_NAND_FMT_XLC_PROG_MID_1ST,		///< QLC program middle page 1st
	FINST_NAND_FMT_XLC_PROG_UPR_1ST,		///< QLC program upper page 1st
	FINST_NAND_FMT_XLC_PROG_TOP_1ST,		///< QLC program top page 1st
	FINST_NAND_FMT_XLC_CB_PROG_LOW,			///< Copyback program low page
	FINST_NAND_FMT_XLC_CB_PROG_MID,			///< 38 Copyback program middle page
	FINST_NAND_FMT_XLC_CB_PROG_UPR,			///< Copyback program upper page
	FINST_NAND_FMT_XLC_CB_PROG_TOP,			///< Copyback program top page
	FINST_NAND_FMT_XLC_CB_PROG_LOW_1ST,		///< Copyback program low page 1st
	FINST_NAND_FMT_XLC_CB_PROG_MID_1ST,		///< Copyback program middle page 1st
	FINST_NAND_FMT_XLC_CB_PROG_UPR_1ST,		///< 43 Copyback program upper page 1st
	FINST_NAND_FMT_XLC_CB_PROG_TOP_1ST,		///< Copyback program top page 1st

	/// Soft Bit Read for SBn
	FINST_NAND_FMT_SLC_SOFT_BIT_SBN_READ,		///< SLC page soft bit read for negative shift (refer to appnote)
	FINST_NAND_FMT_XLC_SOFT_BIT_SBN_READ_LOW,	///< XLC low page soft bit read for negative shift
	FINST_NAND_FMT_XLC_SOFT_BIT_SBN_READ_MID,	///< XLC middle page soft bit read for negative shift
	FINST_NAND_FMT_XLC_SOFT_BIT_SBN_READ_UPR,	///< 48 XLC upper page soft bit read for negative shift
	FINST_NAND_FMT_XLC_SOFT_BIT_SBN_READ_TOP,	///< XLC top page soft bit read for negative shift
	FINST_NAND_FMT_XNOR_SB_CMD1,			///< XNOR between SBn data and SBp data CMD1 CBh
	FINST_NAND_FMT_XNOR_SB_CMD2,			///< XNOR between SBn data and SBp data CMD1 3Fh

	// LUN Status
	FINST_NAND_FMT_LUN0_READ_STATUS,
	FINST_NAND_FMT_LUN1_READ_STATUS,//53

	// BiCS4 support single plane fast read with 36h prefix for shorter tR
	// So split mp version ndcmd fmt here, previous one will be used for SP only
	FINST_NAND_FMT_SLC_READ_CMD_MP,			///< Multi-plane version of SLC page read
	FINST_NAND_FMT_XLC_READ_LOW_MP,			///< Multi-plane version of XLC low page read
	FINST_NAND_FMT_XLC_READ_MID_MP,			///< Multi-plane version of XLC middle page read
	FINST_NAND_FMT_XLC_READ_UPR_MP,			///< Multi-plane version of XLC upper page read
	FINST_NAND_FMT_XLC_READ_TOP_MP,			///< 58 Multi-plane version of XLC top page read

#if !QLC_SUPPORT
	FINST_NAND_FMT_XLC_SELF_ADJUST_READ_LOW,	///< Low page self-adjusting read, refer to M2CMI19-047 BiCS FLASH Gen4 TLC Asynchronous Independent Plane Read Preliminary Rev0.1.pdf
	FINST_NAND_FMT_XLC_SELF_ADJUST_READ_MID,	///< Midle page self-adjusting read
	FINST_NAND_FMT_XLC_SELF_ADJUST_READ_UPR,	///< Upper page self-adjusting read
#else
	FINST_NAND_FMT_XLC_REREAD_SLC,			///< SLC page read retry with 5Dh prefix, refer to M2CMI19-010 BiCS FLASH Gen3 QLC Read Retry with Set Feature Rev0.2.pdf
	FINST_NAND_FMT_XLC_REREAD_LOW,			///< QLC low page read retry with 5Dh prefix
	FINST_NAND_FMT_XLC_REREAD_MID,			///< QLC middle page read retry with 5Dh prefix
	FINST_NAND_FMT_XLC_REREAD_UPR,			///< QLC upper page read retry with 5Dh prefix
	FINST_NAND_FMT_XLC_REREAD_TOP,			///< QLC top page read retry with 5Dh prefix
#endif
	FINST_NAND_FMT_GET_TEMPERATURE, // flash temperature sensor readout
	// Jamie 20210105 Refresh Read w/o 0x36
	FINST_NAND_FMT_REFRESH_READ_SLC,
	FINST_NAND_FMT_REFRESH_READ_LOW,
	FINST_NAND_FMT_REFRESH_READ_MID,
	FINST_NAND_FMT_REFRESH_READ_UPR,
	// Jamie 20210105 Refresh Read w/o 0x36 //
	FINST_NAND_FMT_READ_DQ,  //Sean_add_221229
	FINST_NAND_FMT_WRITE_DQ_PROG,
	FINST_NAND_FMT_WRITE_DQ_READ,
	FINST_NAND_FMT_SingleStatusRead,    // Single Status Read prefix cmd for vth tracking
	FINST_NAND_FMT_SingleStatusRead_read, //Single Status Read cmd for vth_tracking
    FINST_NAND_FMT_XLC_LNA_READ_LOW ,           ///< 72 XLC LNA/DLA read low page
	FINST_NAND_FMT_XLC_LNA_READ_MID,			///< 73 XLC LNA/DLA read middle page
	FINST_NAND_FMT_XLC_LNA_READ_UPR,			///< 74 XLC LNA/DLA read upper page
	FINST_NAND_FMT_SLC_LNA_READ,                ///< 75 SLC LNA/DLA read
#if (Synology_case)
    FINST_NAND_FMT_XLC_GC_PROG_UPR,				///76 Sean_for_Synology_rand_w
#endif
	FINST_NAND_FMT_MAX, 

};
enum {
		FINST_NAND_FMT_VSC_WRITE_DQ_TRAINING_TX_READ = 0,
		FINST_NAND_FMT_VSC_MAX,
};
///< Get nand command format macros
#define FINST_NAND_FMT_TLC_READ(pg_idx)			(FINST_NAND_FMT_XLC_READ_LOW + pg_idx)
#define FINST_NAND_FMT_TLC_READ_RETRY(pg_idx)   (FINST_NAND_FMT_REFRESH_READ_LOW + pg_idx)
#define FINST_NAND_FMT_TLC_READ_MP(pg_idx) 		(FINST_NAND_FMT_XLC_READ_LOW_MP + pg_idx)
#define FINST_NAND_FMT_TLC_CACHE_READ(pg_idx)	(FINST_NAND_FMT_XLC_CACHE_READ_LOW + pg_idx)
#define FINST_NAND_FMT_TLC_PROG(pg_idx)			(FINST_NAND_FMT_XLC_PROG_LOW + pg_idx)
#define FINST_NAND_FMT_QLC_PROG_1ST(pg_idx)		(FINST_NAND_FMT_XLC_PROG_LOW_1ST + pg_idx)
#define FINST_NAND_FMT_TLC_PROG_CACHE(pg_idx)	((pg_idx == (XLC - 1)) ? FINST_NAND_FMT_XLC_CACHE_PROG_UPR : FINST_NAND_FMT_TLC_PROG(pg_idx))

#if (Synology_case)
#define FINST_NAND_FMT_TLC_PROG_GC(pg_idx)		((pg_idx == (XLC - 1)) ? FINST_NAND_FMT_XLC_GC_PROG_UPR : FINST_NAND_FMT_TLC_PROG(pg_idx))
#endif

#define FINST_NAND_FMT_TLC_CB_PROG(pg_idx)		(FINST_NAND_FMT_XLC_CB_PROG_LOW + pg_idx)
#define FINST_NAND_FMT_TLC_SBN_READ(pg_idx)		(FINST_NAND_FMT_XLC_SOFT_BIT_SBN_READ_LOW + pg_idx)
#define FINST_NAND_FMT_TLC_LNA_READ(pg_idx)		(FINST_NAND_FMT_XLC_LNA_READ_LOW + pg_idx)