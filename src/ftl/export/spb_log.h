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
/*! \file spb_log.h
 * @brief a log structure for spb information table, which included erase count and pb type
 *
 * \addtogroup spb_log
 *
 * @{
 *
 * SPB log uses physical nand block to store critical information, SPB info
 * table. This file defines the reconstruction method of SPB log, it allows
 * modules outside FTL to read erase count and pb type back.
 */
#pragma once
#include "ftl_error.h"
#include "log.h"

#define SPB_LOG_SIG		'pbS'  //'LpbS'	AlanCC for wunc///< signature of spb log 
#define NR_SPB_LOG_LUT		7	///< max spb log page for lut
#define NR_SPB_LOG_LUT_DU	(7 << DU_CNT_SHIFT)	///< max spb log lut size in du

/*! @brief define spb log meta, it should be placed after log meta, (DU2) */
typedef struct _spb_log_l2pt_meta_t {
	u32 seed;			///< seed for scramble
	u32 l2pt[NR_SPB_LOG_LUT];	///< l2p table of spb log
} spb_log_l2p_meta_t;

/*! @brief define meta of spb log page */
typedef struct _spb_log_meta_t {
	log_du0_meta_t meta0;		///< meta of du 0 is used in log
	log_du1_meta_t meta1;		///< meta of du 1 is used in log
#if DU_CNT_PER_PAGE == 4
	spb_log_l2p_meta_t l2pt;	///< meta of du 2 is used as l2pt
	log_du3_meta_t meta3;		///< not used
#endif
} spb_log_meta_t;
BUILD_BUG_ON(sizeof(spb_log_meta_t) != PAGE_META_SZ);

#define SPB_LOG_F_FLUSHING	0x01	///< spb log flags for flushing
#define SPB_LOG_F_PENDED	0x02	///< spb log flags for pending
#define SPB_LOG_F_WAIT_FOR_SPB	0x04	///< spb log flags for waiting new SPB

/*!
 * @brief definition of SPB log.
 *
 * This structure should be split for reconstruction and flush two part
 */
typedef struct _spb_log_t {
	log_t log;		///< log design context

	spb_log_meta_t *spb_log_meta;	///< pointer to meta buffer
	u32 meta_idx;			///< meta index of meta buffer
	u8 *ptr;			///< pointer to spb info table
	u32 size;			///< memory resource during reconstruction

	u32 flags;			///< SPB_LOG_F_xxx

	u32 flush_queue;		///< bitmap for queued spb log pages need to be flushed
} spb_log_t;

/*!
 * @brief Initialize and reconstruction SPB log
 *
 * @param pblk	SPBs used by SPB log
 * @param ptr	should be pointer of SPB info table
 * @param size	size of SPB info table
 *
 * @return	return FTL_ERR_OK if latest SPB info table was read
 */
ftl_err_t spb_log_reconstruction(pblk_t pblk[2], void *ptr, u32 size);

/*!
 * @brief Recovery SPB log according LUT
 *
 * @param log_recon	log reconstruction parameter
 *
 * @return		return read status
 */
ftl_err_t spb_log_recovery(log_recon_t *log_recon);

/*! @} */
