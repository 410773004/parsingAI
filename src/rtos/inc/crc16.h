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
/*!
 * \file crc16
 * @brief interface to calculate crc16 with lba and unmapped pattern
 *
 * \addtogroup utility
 * @{
 */

/*!
 * @brief calculate the crc16 based on lba and unmapped pattern
 *
 * @param lba32		host lba in 32 bit
 * @param pattern	unmapped pattern, should be 0 or 1
 *
 * @return	crc value of lba and unmapped pattern
 */
u16 crc16_lkup(u32 lba32, u8 pattern);

u16 crc16_ccitt(const void *buf, int len);
/*! @} */
