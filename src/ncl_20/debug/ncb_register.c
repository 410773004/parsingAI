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
#include "ndcu_reg_access.h"
#include "ncb_ndcu_register.h"
#define __FILEID__ ncbr
#include "trace.h"

/*! \file ncb_register.C
 * @brief dump ndcu register
 *
 * \addtogroup debug
 * \defgroup ncb_register
 * \ingroup debug
 * @{
 *
 */

void dump_ndcu_register(void)
{
	u32 reg;
	u32 val;

	ncl_log_trace(LOG_ERR, 0x28f3, "-------------------\n");
	ncl_log_trace(LOG_ERR, 0x7c1f, "NDCU registers:\n");
	for (reg = 0; reg < sizeof(ncb_ndcu_regs_t); reg += 4) {
		val = ndcu_readl(reg);
		if ((reg & 0xF) == 0) {
			ncl_log_trace(LOG_ERR, 0x7627, "0x%x: ", reg);
		}
		ncl_log_trace(LOG_ERR, 0xcc4e, "0x%x, ", val);
		if ((reg & 0xF) == 0xC) {
			ncl_log_trace(LOG_ERR, 0x63fd, "\n", reg);
		}
	}
	if ((reg & 0xF) != 0x0) {
		ncl_log_trace(LOG_ERR, 0xbc4f, "\n", reg);
	}
	ncl_log_trace(LOG_ERR, 0x771f, "-------------------\n");
}

/*! @} */
