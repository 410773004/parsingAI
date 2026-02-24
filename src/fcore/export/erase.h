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

/*! \file erase.h
 * @brief define ftl core erase handler
 *
 * issue die erase commands for target spb
 *
 * \addtogroup fcore
 * \defgroup erase
 * \ingroup fcore
 * @{
 */
#pragma once
#include "ftltype.h"

/*!
 * @brief erase context to erase a spb via die commands
 */
typedef struct _erase_ctx_t {
	spb_ent_t spb_ent;			///< spb entry, refer to spb_ent_t
	u32 cmpl_cnt;				///< completed die command count
	u32 issue_cnt;				///< issued die command count
	void *caller;				///< caller context
	void (*cmpl)(struct _erase_ctx_t *);	///< callback function when target spb was erased
} erase_ctx_t;

/*!
 * @brief erase a give spb
 *
 * @param ctx	erase context
 *
 * @return	not used
 */
void ftl_core_erase(erase_ctx_t *ctx);

/*! @} */

