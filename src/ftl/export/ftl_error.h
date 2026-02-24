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
/*! \file ftl_error.h
 * @brief export ftl interface, definie ftl error code
 *
 * \addtogroup ftl_export
 * @{
 */
#pragma once

/** just define it, todo: group them */
typedef enum {
	FTL_ERR_OK = 0,
	FTL_ERR_CLEAN,
	FTL_ERR_UC_ERROR,
	FTL_ERR_ABORTED,
	FTL_ERR_PENDING,
	FTL_ERR_VIRGIN,
	FTL_ERR_NEED_RECON,
	FTL_ERR_RECON_FAILED,
	FTL_ERR_RECON_DONE,

	FTL_ERR_OOM,
	FTL_ERR_OOR,
	FTL_ERR_BLOCKED,

	FTL_ERR_NEED_GC,
	FTL_ERR_BUSY,
	FTL_ERR_IDLE
} ftl_err_t;

/*! @} */
