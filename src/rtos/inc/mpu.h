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

//#if (CUST_CODE == 0)    //RD only  Eric20231027
#define OTP_PROTECT //_GENE_20211004
//#endif
#define MAX_MPU_REGION		16

#define REGION_SIZE_256    0x07
#define REGION_SIZE_32K    0x0E
#define REGION_SIZE_64K    0x0F
#define REGION_SIZE_128K   0x10
#define REGION_SIZE_256K   0x11
#define REGION_SIZE_512K   0x12
#define REGION_SIZE_1M     0x13
#define REGION_SIZE_2M     0x14
#define REGION_SIZE_256M   0x1b
#define REGION_SIZE_512M   0x1c
#define REGION_SIZE_1G     0x1d
#define REGION_SIZE_2G     0x1e

#define KBYTES	(1 << 10)
#define MBYTES	(1 << 20)
#define GBYTES	(1 << 30)

enum {
	AP_NO_ACCESS	= 0x0,	/* All access generate a permission fault */
	AP_PRIVILEGED	= 0x1,	/* Previleged access only */
	AP_USR_W_FAULT	= 0x2,	/* Writes in User mode generate permission fault */
	AP_FULL_ACCESS	= 0x3,	/* Full access */
	AP_UNP1		= 0x4,	/* Reserved */
	AP_R_PRIVILEGED	= 0x5,	/* Previleged read only */
	AP_RD_ONLY	= 0x6,	/* Previleged / User read only */
	AP_UNP2		= 0x7,	/* Reserved */
};

enum {
	XN_DISABLE	= 0,	/* All instruction fetches ENd */
	XN_ENABLE	= 1,	/* No instruction fetches ENd */
};

/*!
 * @brief	when fixed TEX[2]=1, the memory region is cachable region,
 *		then TEX[1:0] and [C,B] define the Inner and Outer cached policy
 */
enum cached_policy_t{
	NON_CACHEABLE	= 0b00, /* Non-Cacheable */
	WBWA 		= 0b01,	/* Write-Back, Write-Allocate */
	WTNOWA		= 0b10,	/* Write-Through, No Write-Allocate */
	WBNOWA		= 0b11,	/* Write-Back, No Write-Allocate */
};

enum {
	MPU_NON_SHARED	= 0,
	MPU_SHARED	= 1,
};

struct mpu_region
{
	u32 base; /* 32 bytes aligned */

	union {
		u32 all;
		struct {
			u32 EN     : 1;
			u32 size   : 5;
			u32 rsvd1  : 2;
			u32 sub_region_dis : 8;
			u32 rsvd2  : 16; /* SBZ*/
		} b;
	} size_enable;

	/* Hold the region attributes and access permission */
	union {
		u32 all;
		struct {
			u32 B_C   : 2; /* [0]B bit and [1]C bit */
			u32 S     : 1; /* Sharability */
			u32 TEX   : 3; /* Type extention */
			u32 rsvd1 : 2; /* SBZ */
			u32 AP    : 3; /* Access Permission */
			u32 rsvd2 : 1;
			u32 XN    : 1; /* Execute Never, 0: all instruction fetches ENd, 1: no instruction fetches ENd */
			u32 rsvd3 : 19;
		} b;
	} access_control;
};

#define set_normal_range(addr, sz, l1, l2, sh)	\
	{.base = addr, .size_enable = {.b = {.size = sz, .EN = 1}}, .access_control = {.b = {.TEX = 0b100 | l2, .B_C = l1, .AP = AP_FULL_ACCESS, .S = sh}}}

#define set_strong_order_range(addr, sz)	\
	{.base = addr, .size_enable = {.b = {.size = sz, .EN = 1}}, .access_control = {.b = {.TEX = 0, .B_C = 0, .AP = AP_FULL_ACCESS,}}}
#define set_strong_order_range_ro(addr, sz)	\
	{.base = addr, .size_enable = {.b = {.size = sz, .EN = 1}}, .access_control = {.b = {.TEX = 0, .B_C = 0, .AP = AP_RD_ONLY,}}}

extern u32 add_mpu_region(struct mpu_region *regions);
extern void set_mpu_region(void);
/*!
 * @brief Generate TCM ECC Code and enable ECC Detecting
 *
 * @param	none
 *
 * @return	none
 */
extern void set_tcm_ecc(void);
