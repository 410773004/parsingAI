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

#ifndef _BITOPS_H_
#define _BITOPS_H_
/*lint -e{530} ret is initialized in assembly*/
static inline unsigned int clz(unsigned int x)
{
	unsigned int ret;

	__asm__ volatile("clz\t%0, %1" : "=r" (ret) : "r" (x));
	return ret;
}

#define test_bit(nr, ptr)	((((u32 *)ptr)[(nr) >> 5]) & (1 << ((nr) & 0x1F)))

#define LOG2(X) (31 - clz(X))

/*
 * Native endian assembly bitops.  nr = 0 -> word 0 bit 0.
 */
extern void set_bit(int nr, volatile void * p);
extern void clear_bit(int nr, volatile void * p);
extern void change_bit(int nr, volatile void * p);
extern int test_and_set_bit(int nr, volatile void * p);
extern int test_and_clear_bit(int nr, volatile void * p);
extern int test_and_change_bit(int nr, volatile void * p);

/*
 * Little endian assembly bitops.  nr = 0 -> byte 0 bit 0.
 */
extern int find_first_zero_bit(const void * p, unsigned size);
extern int find_next_zero_bit(const void * p, int size, int offset);
extern int find_first_bit(const void *p, unsigned size);
extern int find_next_bit(const void *p, int size, int offset);

/*
 * Big endian assembly bitops.  nr = 0 -> byte 3 bit 0.
 */
extern int find_first_zero_bit_be(const void * p, unsigned size);
extern int find_next_zero_bit_be(const void * p, int size, int offset);
extern int find_first_bit_be(const void *p, unsigned size);
extern int find_next_bit_be(const void *p, int size, int offset);

#endif
