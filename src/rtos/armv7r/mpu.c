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

#include <types.h>
#include "assert.h"
#include "sect.h"
#include "mpu.h"
#include "mpc.h"
#include "stdlib.h"
#include "string.h"
#include "rainier_soc.h"

/*lint -esym(551, _regions) is accessed */
fast_data_zi u8 mpu_region_cnt = 0;
fast_data struct mpu_region _regions[MAX_MPU_REGION] =
{
	set_normal_range(ATCM_BASE,			REGION_SIZE_128K,	NON_CACHEABLE, NON_CACHEABLE,	MPU_NON_SHARED),
	set_normal_range(BTCM_BASE,			REGION_SIZE_256K,	NON_CACHEABLE, NON_CACHEABLE,	MPU_NON_SHARED),
// 	set_normal_range(BTCM_BASE + (u32) 128*KBYTES,	REGION_SIZE_64K,	NON_CACHEABLE, NON_CACHEABLE,	MPU_NON_SHARED),
	set_normal_range(BTCM_SH_BASE,			REGION_SIZE_64K,	NON_CACHEABLE, NON_CACHEABLE,	MPU_SHARED),
	set_normal_range(BTCM_SH_BASE + (u32) 64*KBYTES,REGION_SIZE_64K,	NON_CACHEABLE, NON_CACHEABLE,	MPU_SHARED),
	set_normal_range(CPU1_ATCM_BASE,		REGION_SIZE_1M,		NON_CACHEABLE, NON_CACHEABLE,	MPU_NON_SHARED),
	set_normal_range(CPU3_ATCM_BASE,		REGION_SIZE_1M,		NON_CACHEABLE, NON_CACHEABLE,	MPU_NON_SHARED),
	//set_normal_range(ROM_BASE,			REGION_SIZE_32K, 	NON_CACHEABLE, NON_CACHEABLE, MPU_NON_SHARED),
	set_normal_range(ROM_BASE,			REGION_SIZE_128K, 	NON_CACHEABLE, NON_CACHEABLE, MPU_NON_SHARED),
	set_normal_range(SRAM_BASE,			REGION_SIZE_2M,		NON_CACHEABLE, NON_CACHEABLE,	MPU_NON_SHARED),
	set_normal_range(RAID_BASE,			REGION_SIZE_1M,	NON_CACHEABLE, NON_CACHEABLE,	MPU_NON_SHARED),
	//set_normal_range(RAID_BASE + (u32) 512*KBYTES,REGION_SIZE_32K,	NON_CACHEABLE, NON_CACHEABLE,	MPU_NON_SHARED),
	set_normal_range(SPRM_BASE,			REGION_SIZE_256K,	NON_CACHEABLE, NON_CACHEABLE,	MPU_NON_SHARED),
	set_normal_range(L2CRAM_BASE,			REGION_SIZE_128K,	NON_CACHEABLE, NON_CACHEABLE,	MPU_NON_SHARED),
	set_normal_range(DDR_BASE,			REGION_SIZE_1G,		NON_CACHEABLE, WTNOWA, 		MPU_SHARED),
	set_normal_range(DDR_BASE + (u32) GBYTES,	REGION_SIZE_1G,		NON_CACHEABLE, WTNOWA,		MPU_SHARED),

	set_strong_order_range(0xC0000000, REGION_SIZE_256M),
	#ifdef OTP_PROTECT
	//avoid write otp
    set_strong_order_range_ro(0xC0042100, REGION_SIZE_256),
    set_strong_order_range_ro(0xC0042200, REGION_SIZE_256),
	#endif
};

fast_code u32 add_mpu_region(struct mpu_region *regions)
{
	u32 cur = (u32) mpu_region_cnt;

	sys_assert(mpu_region_cnt < MAX_MPU_REGION);

	_regions[cur] = *regions;
 	__asm__ __volatile__("mcr p15, 0, %0, c6, c2, 0" :: "r"(cur) : "memory", "cc");				/* Write RGNR */
	__asm__ __volatile__("mcr p15, 0, %0, c6, c1, 0" :: "r"(regions->base) : "memory", "cc");			/* Write MPU Region Base Address Register*/
	__asm__ __volatile__("mcr p15, 0, %0, c6, c1, 2" :: "r"(regions->size_enable.all) : "memory", "cc");		/* Write Data MPU Region Size and Enable Register */
	__asm__ __volatile__("mcr p15, 0, %0, c6, c1, 4" :: "r"(regions->access_control.all) : "memory", "cc");	/* Write MPU Region Access Controll Register */
	mpu_region_cnt++;
	return cur;
}

/*!
 * @brief this function should be accessible in Privileged mode only, etc SVC
 *
 * @return	None
 */
 /*lint -e{438, 530, 550, 551} */
fast_code void set_mpu_region(void)
{
	u32 i;
	u32 sctlr;
	#ifndef OTP_PROTECT
	u32 size;
	#endif

	union {
		u32 all;

		struct {
			u32 S       : 1;
			u32 rsvd1   : 7;
			u32 dregion : 8;
			u32 rsvd2   : 16;
		} b;
	} mpuir;

	__asm__ __volatile__("mrc p15, 0, %0, c0, c0, 4" : "=r"(mpuir.all) :: "memory", "cc");

	sys_assert(mpuir.b.dregion == MAX_MPU_REGION);

	for (i = 0; i < MAX_MPU_REGION; ++i) {
		//check mpu region's format
		#ifndef OTP_PROTECT
		size = (u32) ((32*KBYTES) << (_regions[i].size_enable.b.size - REGION_SIZE_32K));
		sys_assert(!(_regions[i].base % (u32)(32*KBYTES))); 								/* base must 32K alignment */
		sys_assert(!(_regions[i].base % size));	/* base must size's multiple alignment */
		#endif
		__asm__ __volatile__("mcr p15, 0, %0, c6, c2, 0" :: "r"(i) : "memory", "cc");					/* Write RGNR */
		__asm__ __volatile__("mcr p15, 0, %0, c6, c1, 0" :: "r"(_regions[i].base) : "memory", "cc");			/* Write MPU Region Base Address Register*/
		__asm__ __volatile__("mcr p15, 0, %0, c6, c1, 2" :: "r"(_regions[i].size_enable.all) : "memory", "cc");	/* Write Data MPU Region Size and Enable Register */
		__asm__ __volatile__("mcr p15, 0, %0, c6, c1, 4" :: "r"(_regions[i].access_control.all) : "memory", "cc");	/* Write MPU Region Access Controll Register */
		if (_regions[i].size_enable.b.EN == 1)
			mpu_region_cnt++;
	}

	/* Enable MPU */
	__asm__ __volatile__("mrc p15, 0, %0, c1, c0, 0" : "=r"(sctlr) :: "memory", "cc");					/* Read SCTLR */
	sctlr |= 1;
	__asm__ __volatile__("dsb");
	__asm__ __volatile__("mcr p15, 0, %0, c1, c0, 0" :: "r"(sctlr) : "memory", "cc");					/* Write SCTLR */
	__asm__ __volatile__("isb");
}

slow_code void set_tcm_ecc(void)
{
	u32 actlr;
	extern void *atcm_base;
	extern void *btcm_base;

	/*
	 * Generate ECC Code by Read Modify Write ATCM and BTCM
	 * Only CPU 1 would generate ECC Code for BTCM_SH
	 */

	memcpy32(&atcm_base, &atcm_base, ATCM_SIZE);
#if CPU_ID_0 == 0
	memcpy32(&btcm_base, &btcm_base, BTCM_SIZE + BTCM_SH_SIZE);
	local_item_done(tcm_ecc);
#else
	memcpy32(&btcm_base, &btcm_base, BTCM_SIZE);
	wait_remote_item_done_no_poll(tcm_ecc);
#endif

	/* Enable ECC Detecting */
	__asm__ __volatile__("isb");
	__asm__ __volatile__("mrc p15, 0, %0, c1, c0, 1" : "=r"(actlr) :: "memory", "cc");					/* Read ACTLR */
	actlr |= (BIT25 | BIT26 | BIT27);																	/* enable ATCM/B0TCM/B1TCM ECC Detecting */
	__asm__ __volatile__("mcr p15, 0, %0, c1, c0, 1" :: "r"(actlr) : "memory", "cc");					/* Write ACTLR */
	__asm__ __volatile__("isb");
}

#if defined(SAVE_CDUMP)
ddr_code void mpu_exit(void)
{
	u32 sctlr = 0;
	/*
		2. Clean and invalidate the data caches.
		3. Disable caches.
		4. Invalidate the instruction cache
		In statup.S we will invalidate all data cache(write-through) and instruction cache, so just disable them here
	*/
	__asm__ __volatile__("mrc p15, 0, %0, c1, c0, 0" : "=r"(sctlr) :: "memory", "cc");  /* read SCTLR */
	sctlr &= ~((1 << 0) | (1 << 2) | (1 << 12)); ///< disable I-cache, D-cache & MPU
	__asm__ __volatile__("dsb");
	__asm__ __volatile__("mcr p15, 0, %0, c1, c0, 0" :: "r"(sctlr) : "memory", "cc");  /* write SCTLR */
	__asm__ __volatile__("isb");
}
#endif
