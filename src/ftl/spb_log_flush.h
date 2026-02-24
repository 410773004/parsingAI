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
/*! \file spb_log_flush.h
 * @brief a log structure for spb information table, which included erase count and pb type
 *
 * \addtogroup spb_log
 *
 * @{
 *
 * SPB log uses physical nand block to store critical information, SPB info
 * table. This file defines the reconstruction method of SPB log, it allows
 * modules outside FTL to read erase count and pb type back.
 *
 * Flush method of SPB log is not exported.
 */
#pragma once

/*!
 * @brief Initialize SPB log flush resource
 *
 * @return	none
 */
//void spb_log_flush_init(void);  //Curry

/*!
 * @brief Interface to flush a SPB log page, or flush all SPB log if page_id == ~0
 *
 * @param page_id	page to be flushed, ~0 mean all pages need to be flushed
 *
 * @return		not used
 */
void spb_log_flush(u32 log_page_id);

/*!
 * @brief Check if spb log free page count was enough for flushing
 *
 * @return		return true for GC trigger
 */
bool spb_log_gc_check(void);

/*!
 * @brief Check if spb log free page count was enough for reconstruction critical gc
 *
 * @return		not used
 */
void spb_log_critical_chk(void);

/*!
 * @brief Resume and reconstruction SPB log
 *
 * @return		not use
 */
void spb_log_resume(void);

/*!
 * @brief dump function of spb log
 *
 * @return		not used
 */
void spb_log_dump(void);
/*! @} */
