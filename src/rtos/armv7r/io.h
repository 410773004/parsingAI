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

#ifndef _IO_H_
#define _IO_H_
/*lint -e{530} */
static inline __attribute__((always_inline)) void writel(u32 val, void *addr)
{
	__asm__ volatile("str %1, %0"
		:
		: "Qo" (*(volatile u32 *)addr), "r" (val));
}

/*lint -e{530} */
static inline __attribute__((always_inline)) u32 readl(const void *addr)
{
	u32 val;

	__asm__ volatile("ldr %0, %1"
		: "=r" (val)
		: "Qo" (*(volatile u32 *)addr));
	return val;
}

static inline  __attribute__((always_inline)) u8 get_cpu_id(void)
{
	u32 cpu_id;

	__asm__ volatile("MRC p15, 0, %0, c0, c0, 5" : "=r" (cpu_id) : );

	return (cpu_id & (0xf << 8)) >> 8;
}
#define isb(option) __asm__ __volatile__ ("isb " #option : : : "memory")
#define dsb(option) __asm__ __volatile__ ("dsb " #option : : : "memory")
#define dmb(option) __asm__ __volatile__ ("dmb " #option : : : "memory")
#endif
