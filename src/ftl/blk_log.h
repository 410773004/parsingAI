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
/*! \file blk_log.h
 * @brief define physical block logs, the physical block table will be saved in this log
 *
 * \addtogroup ftl
 *
 * @{
 */
#pragma once
#include "event.h"

extern u8 evt_flush_blk_log;			///< event to flush block log

/*!
 * @brief block log init, reconstruction block log pool
 *
 * @note if block log can't be read, it will be restored from FRB
 *
 * @param frb_valid	if FRB was valid or not
 *
 * @return		not used
 */
void blk_log_start(bool frb_valid);

/*!
 * @brief flush block log in event handler
 *
 * @return		not used
 */
static inline void blk_log_flush_trigger(void)
{
	evt_set_cs(evt_flush_blk_log, 0, 0, CS_TASK);
}

/*! @} */
