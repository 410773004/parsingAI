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

#include "pf_reg.h"
#include "flex_misc.h"
#include "flex_map.h"
#include "flex_dbl.h"
#include "flex_addr.h"

///  NVME Control Base Register
#define PF_REG_ADDR			(NVME_BASE + 0x0000)
#define VF_REG_ADDR			(NVME_BASE + 0x3000)
#define FLEX_DBL_REG_ADDR		(NVME_BASE + 0x4000)
#define FLEX_ADDR_REG_ADDR		(NVME_BASE + 0x5000)
#define FLEX_MAP_REG_ADDR		(NVME_BASE + 0x6000)
#define FLEX_MISC_REG_ADDR		(NVME_BASE + 0x7000)

//-----------------------------------------------------------------------------
//  Inline Functions:
//-----------------------------------------------------------------------------
static inline void pf_writel(vu32 val, u32 reg)
{
	writel(val, (void*) (PF_REG_ADDR + reg));
}

static inline vu32 pf_readl(u32 reg)
{
	return readl((const void*) (PF_REG_ADDR + reg));
}

static inline void vf_writel(vu32 val, u32 reg)
{
	writel(val, (void*) (VF_REG_ADDR + reg));
}

static inline vu32 vf_readl(u32 reg)
{
	return readl((const void*) (VF_REG_ADDR + reg));
}

static inline void flex_dbl_writel(vu32 val, u32 reg)
{
	writel(val, (void*) (FLEX_DBL_REG_ADDR + reg));
}

static inline vu32 flex_dbl_readl(u32 reg)
{
	return readl((const void*) (FLEX_DBL_REG_ADDR + reg));
}

static inline void flex_addr_writel(vu32 val, u32 reg)
{
	writel(val, (void*) (FLEX_ADDR_REG_ADDR + reg));
}

static inline vu32 flex_addr_readl(u32 reg)
{
	return readl((const void*) (FLEX_ADDR_REG_ADDR + reg));
}

static inline void flex_map_writel(vu32 val, u32 reg)
{
	writel(val, (void*) (FLEX_MAP_REG_ADDR + reg));
}

static inline vu32 flex_map_readl(u32 reg)
{
	return readl((const void*) (FLEX_MAP_REG_ADDR + reg));
}

static inline void flex_misc_writel(vu32 val, u32 reg)
{
	writel(val, (void*) (FLEX_MISC_REG_ADDR + reg));
}

static inline vu32 flex_misc_readl(u32 reg)
{
	return readl((const void*) (FLEX_MISC_REG_ADDR + reg));
}
