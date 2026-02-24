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

#include "ncb_eccu_register.h"
#include "dec_top_ctrl_register.h"
#include "eccu_mdec_register.h"
#include "raid_top_register.h"

#ifndef _ECCU_REG_ACCESS_H_
#define _ECCU_REG_ACCESS_H_

///  ECCU Base Register
#define ECCU_REG_ADDR			(NCB_BASE + 0x0000)
#define DEC_TOP_REG_ADDR		(ECCU_REG_ADDR + 0x0200)
#define MDEC_REG_ADDR			(ECCU_REG_ADDR + 0x0300)
#define PDEC_REG_ADDR			(ECCU_REG_ADDR + 0x0500)
#define ENC_REG_ADDR			(ECCU_REG_ADDR + 0x0700)
#define RAID_TOP_REG_ADDR		(ECCU_REG_ADDR + 0xF000)
#define NCB_TOP_WRAP_BASE		(ECCU_REG_ADDR + 0xF100)

//-----------------------------------------------------------------------------
//  Inline Functions:
//-----------------------------------------------------------------------------
static inline void eccu_writel(vu32 val, u32 reg)
{
	writel(val, (void*) (ECCU_REG_ADDR + reg));
}

static inline vu32 eccu_readl(u32 reg)
{
	return readl((const void*) (ECCU_REG_ADDR + reg));
}

static inline void dec_top_writel(vu32 val, u32 reg)
{
	writel(val, (void*) (DEC_TOP_REG_ADDR + reg));
}

static inline vu32 dec_top_readl(u32 reg)
{
	return readl((const void*) (DEC_TOP_REG_ADDR + reg));
}

static inline void mdec_writel(vu32 val, u32 reg)
{
	writel(val, (void*) (MDEC_REG_ADDR + reg));
}

static inline vu32 mdec_readl(u32 reg)
{
	return readl((const void*) (MDEC_REG_ADDR + reg));
}

static inline void pdec_writel(vu32 val, u32 reg)
{
	writel(val, (void*) (PDEC_REG_ADDR + reg));
}

static inline vu32 pdec_readl(u32 reg)
{
	return readl((const void*) (PDEC_REG_ADDR + reg));
}

static inline void enc_writel(vu32 val, u32 reg)
{
	writel(val, (void*) (ENC_REG_ADDR + reg));
}

static inline vu32 enc_readl(u32 reg)
{
	return readl((const void*) (ENC_REG_ADDR + reg));
}

static inline void raid_top_writel(vu32 val, u32 reg)
{
	writel(val, (void*) (RAID_TOP_REG_ADDR + reg));
}

static inline vu32 raid_top_readl(u32 reg)
{
	return readl((const void*) (RAID_TOP_REG_ADDR + reg));
}

static inline void ncb_top_wrap_writel(vu32 val, u32 reg)
{
	writel(val, (void*) (NCB_TOP_WRAP_BASE + reg));
}

static inline vu32 ncb_top_wrap_readl(u32 reg)
{
	return readl((const void*) (NCB_TOP_WRAP_BASE + reg));
}

#endif

