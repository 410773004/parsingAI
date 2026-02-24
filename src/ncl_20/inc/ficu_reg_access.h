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

#include "ncb_ficu_register.h"
#include "io.h"

#ifndef _FICU_REG_ACCESS_H_
#define _FICU_REG_ACCESS_H_

///  FICU Base Register
#define FICU_REG_ADDR			(NCB_BASE + 0x1000)

//-----------------------------------------------------------------------------
//  Inline Functions:
//-----------------------------------------------------------------------------
static inline void ficu_writel(vu32 val, u32 reg)
{
	writel(val, (void*) (FICU_REG_ADDR + reg));
}

static inline vu32 ficu_readl(u32 reg)
{
	return readl((const void*) (FICU_REG_ADDR + reg));
}

static inline void btn_me_ctrl_writel(vu32 val, u32 reg)
{
    writel(val, (void*) (BTN_ME_BASE + reg));
}

static inline vu32 btn_me_ctrl_readl(u32 reg)
{
    return readl((const void*) (BTN_ME_BASE + reg));
}

#endif

