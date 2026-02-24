//-----------------------------------------------------------------------------
//
// Copyright(c) 2016-2017 InnoGrit Technologies Incorporated
// All Rights reserved.
//
// InnoGrit solves the data storage and data transport problem in
// artificial intelligence and other big data applications
// through innovative integrated circuit (IC) and system solutions.
//
//-----------------------------------------------------------------------------

#include "ncb_ndcu_register.h"
#include "ndphy_register.h"

#ifndef _NDCU_REG_ACCESS_H_
#define _NDCU_REG_ACCESS_H_

#define NDCU_REG_ADDR		(NCB_BASE + 0x2000)
#define NDPHY_REG_ADDR(_ch)	(NCB_BASE + 0x3000 + (_ch << 8))

//-----------------------------------------------------------------------------
//  Inline Functions:
//-----------------------------------------------------------------------------
static inline void ndcu_writel(vu32 val, u32 reg)
{
	writel(val, (void*) (NDCU_REG_ADDR + reg));
}

static inline vu32 ndcu_readl(u32 reg)
{
	return readl((void*) (NDCU_REG_ADDR + reg));
}

static inline void ndphy_writel(int ch, vu32 val, u32 reg)
{
	writel(val, (void*) (NDPHY_REG_ADDR(ch) + reg));
}

static inline vu32 ndphy_readl(int ch, u32 reg)
{
	return readl((void*) (NDPHY_REG_ADDR(ch) + reg));
}

#endif
