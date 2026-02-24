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
/*! \file ftlprecomp.h
 * @brief include default header and define some api
 *
 * \addtogroup ftl
 * \defgroup ftl
 * \ingroup ftl
 * @{
 */
#pragma once
#include "sect.h"
#include "stdlib.h"
#include "string.h"
#include "assert.h"
#include "types.h"
#include "rainier_soc.h"
#include "dma.h"
#include "pool.h"
#include "ftl.h"
#include "ftl_export.h"
#include "ftltype.h"
#include "dtag.h"
#include "rdisk.h"
#include "nvme_spec.h"

#define FTL_DONT_USE_REMAP_SPB		0	///< 1 if don't use remapped SPB

/* define grown defect type, 8-bit */
#define GRWN_DEF_TYPE_ERASE		0x00	///< erase error
#define GRWN_DEF_TYPE_PROG		0x10	///< program error
#define GRWN_DEF_TYPE_PROG_SLC		0x11	///< slc program error
#define GRWN_DEF_TYPE_PROG_XLC		0x12	///< xlc program error
#define GRWN_DEF_TYPE_READ		0x20	///< read error
#define GRWN_DEF_TYPE_READ_GC		0x21	///< gc read error
#define GRWN_DEF_TYPE_OTHR		0xF0	///< other
#define GRWN_DEF_TYPE_OTHR_MANUL	0xF1	///< manually added
#define GRWN_DEF_TYPE_OTHR_UNKN		0xF2	///< unknown

typedef struct _ftl_defect_t {
	u32 df_cnt;
	u32 df[MAX_INTERLEAVE_DW_IN_FTL];
} ftl_defect_t;

extern bool mapping_clean;
extern bool more_die;

static inline fast_code void bitmap_set(u32 *bitmap, u32 index)
{
	u32 idx = index >> 5;
	u32 off = index & 0x1F;

	bitmap[idx] |= (1 << off);
}

/*! @} */
