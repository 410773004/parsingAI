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

#ifndef _CPU_ENDIAN_H_
#define _CPU_ENDIAN_H_

static inline u16 get_unaligned_le16(const u8 *p)
{
	return p[0] | p[1] << 8;
}

static inline u32 get_unaligned_le32(const u8 *p)
{
	return p[0] | p[1] << 8 | p[2] << 16 | p[3] << 24;
}

#define le32_to_cpu(x) (x)

#define le16_to_cpu(x) (x)

#define cpu_to_le32(x) (x)

#define cpu_to_le16(x) (x)

#define uale32_to_cpu(x) le32_to_cpu(get_unaligned_le32((const u8 *)&x))

#define uale16_to_cpu(x) le16_to_cpu(get_unaligned_le16((const u8 *)&x))

static inline void cpu_to_be64(u64 data, u8 *buf)
{
	buf[0] = data >> 56;
	buf[1] = data >> 48;
	buf[2] = data >> 40;
	buf[3] = data >> 32;
	buf[4] = data >> 24;
	buf[5] = data >> 16;
	buf[6] = data >>  8;
	buf[7] = data;
}

static inline u64 be64_to_cpu(u8 *ptr)
{
	u64 tmp = 0x0000000000000000UL;

	tmp =   (((u64)ptr[0])<<56) + (((u64)(ptr[1]))<<48) +
		(((u64)ptr[2])<<40) + (((u64)(ptr[3]))<<32) +
		(((u64)ptr[4])<<24) + (((u64)(ptr[5]))<<16) +
		(((u64)ptr[6])<<8)  + ((u64)(ptr[7]));
	return tmp;
}

static inline void cpu_to_be32(u32 data, u8 *buf)
{
	buf[0] = data >> 24;
	buf[1] = data >> 16;
	buf[2] = data >>  8;
	buf[3] = data;
}

static inline u32 be32_to_cpu(u8 *ptr)
{
	return (ptr[0]<<24) + (ptr[1]<<16) + (ptr[2]<<8) + ptr[3];
}

static inline void cpu_to_be16(u16 data, u8 *buf)
{
	buf[0] = data >>  8;
	buf[1] = data;
}

static inline u16 be16_to_cpu(u8 *ptr)
{
	return (ptr[0]<<8) + ptr[1];
}

#endif
