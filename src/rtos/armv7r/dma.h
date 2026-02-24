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

/*! \file dma.h
 * @brief dma address translation
 *
 * \addtogroup rtos
 * \defgroup dma
 * \ingroup rtos
 *
 * {@
 */

#ifndef _DMA_H_
#define _DMA_H_

#include "rainier_soc.h"
#include "assert.h"
#ifndef __FILEID__
#define __FILEID__ dma
#endif
#include "trace.h"
#if CPU_ID == 1
#define REMOTE_BTCM_BASE	CPU1_BTCM_BASE
#define REMOTE_BTCM_SIZE	CPU1_BTCM_SIZE
#elif CPU_ID == 2
#define REMOTE_BTCM_BASE	CPU2_BTCM_BASE
#define REMOTE_BTCM_SIZE	CPU2_BTCM_SIZE
#elif CPU_ID == 3
#define REMOTE_BTCM_BASE	CPU3_BTCM_BASE
#define REMOTE_BTCM_SIZE	CPU3_BTCM_SIZE
#else
#define REMOTE_BTCM_BASE	CPU4_BTCM_BASE
#define REMOTE_BTCM_SIZE	CPU4_BTCM_SIZE
#endif

extern u32 btcm_dma_base;	///< btcm dma base per CPU

/*!
 * @brief translate sram memory pointer to dma address for asic
 *
 * @param p	sram memory pointer
 *
 * @return	dma address
 */
static u32 inline sram_to_dma(void *p)
{
	u32 addr = (u32)p;
	return addr & (ALIGNED_SRAM_SIZE - 1);
}

static u32 inline share_btcm_to_dma(void *p)
{
	u32 a = (u32 ) p;

	a -= BTCM_BASE;
	a += CPU1_BTCM_BASE;
	return a;
}

/*!
 * @brief translate btcm memory pointer to dma address for asic
 *
 * @param p	btcm memory pointer
 *
 * @return	dma address
 */
static u32 inline btcm_to_dma(void *p)
{
#ifdef MPC
	if ((u32) p >= CPU1_BTCM_BASE)
		return (u32) p;

	if ((u32 )p >= BTCM_SH_BASE)
		return share_btcm_to_dma(p);
#endif
	u32 addr = (u32) ((u32)p - BTCM_BASE);

	if (addr >= (BTCM_SIZE + BTCM_SH_SIZE)) {
		rtos_arch_trace(LOG_ERR, 0x39a3, "addr %x\n", addr);
		panic("stop");
	}
	addr += btcm_dma_base;
	return addr;
}

/*!
 * @brief translate dma address to sram memory pointer
 *
 * @param dma	dma address
 *
 * @return	sram memory pointer
 */
static void inline *dma_to_sram(u32 dma)
{
	u32 addr = dma | SRAM_BASE;
	return (void *)addr;
}

/*!
 * @brief translate dma address to btcm memory pointer
 *
 * @param dma	dma address
 *
 * @return	btcm memory pointer
 */
static void inline *dma_to_btcm(u32 dma)
{
	if (dma > btcm_dma_base) {
		u32 addr = dma - btcm_dma_base;
		addr |= BTCM_BASE;
		return (void *)addr;
	}
	return (void *) dma;
}

/*!
 * @brief translate ddr memory pointer to ddr address
 *
 * @note this api only suppport 1st 2G of DDR
 *
 * @param p	ddr memory pointer
 *
 * @return	dma address
 */
static u32 inline ddr_to_dma(void *p)
{
	u32 addr = (u32) (p - DDR_BASE);
	return addr &= (DDR_SIZE - 1);
}

/*!
 * @brief translate local tcm memory to remote tcm address
 *
 * @param p	local tcm memory pointer
 *
 * @return	remote tcm pointer
 */
static inline void *tcm_local_to_share(void *p)
{
	if (p == NULL)
		return NULL;

	u32 addr = (u32) p;
	sys_assert(addr >= BTCM_BASE && addr < (BTCM_BASE + BTCM_SIZE));
	addr -= BTCM_BASE;
	addr += REMOTE_BTCM_BASE;
	return (void *) addr;
}

/*!
 * @brief translate remote tcm memory to local tcm address
 *
 * @param p	remote tcm memory pointer
 *
 * @return	local tcm pointer
 */
static inline void *tcm_share_to_local(void *p)
{
	if (p == NULL)
		return NULL;

	u32 addr = (u32) p;
	sys_assert(addr >= REMOTE_BTCM_BASE && addr < (REMOTE_BTCM_BASE + REMOTE_BTCM_SIZE));
	addr -= REMOTE_BTCM_BASE;
	addr += BTCM_BASE;
	return (void *) addr;
}

/*!
 * @brief check if address was BTCM
 *
 * @param p		address
 *
 * @return		return true if BTCM
 */
static inline bool is_btcm(void *p)
{
	u32 addr = (u32) p;

	if ((BTCM_BASE <= addr && addr < BTCM_BASE + BTCM_SIZE + BTCM_SH_SIZE) ||
		(CPU1_BTCM_BASE <= addr && addr < CPU1_BTCM_BASE + BTCM_SIZE + BTCM_SH_SIZE) ||
		(CPU2_BTCM_BASE <= addr && addr < CPU2_BTCM_BASE + BTCM_SIZE + BTCM_SH_SIZE) ||
		(CPU3_BTCM_BASE <= addr && addr < CPU3_BTCM_BASE + BTCM_SIZE + BTCM_SH_SIZE) ||
		(CPU4_BTCM_BASE <= addr && addr < CPU4_BTCM_BASE + BTCM_SIZE + BTCM_SH_SIZE)) {
		return true;
	}

	return false;
}

/*!
 * @brief memory pointer to DMA address
 *
 * @param p	pointer
 *
 * @return	return DMA address of pointer
 */
static inline u32 mem_to_dma(void *p)
{
	u32 addr = (u32) p;

	if (addr >= DDR_BASE)
		return ddr_to_dma(p);
	else if (addr >= SRAM_BASE)
		return sram_to_dma(p);
	else
		return btcm_to_dma(p);
}

#undef __FILEID__
#endif

/*! @} */
