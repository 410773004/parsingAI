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
 * \file crc32
 * @brief interface to calculate crc32
 *
 * \addtogroup utility
 * @{
 */

/*!
 * @brief calculate the crc-32 based on crc-32 table
 *
 * @param buf	pointer to the source data buffer
 * @param size	number of elements in the source data buffer.
 *
 * @return	new checksum value
 */
extern u32 crc32(const void *buf, int size);
extern u32 crc32_cont(const void *buf, int size, u32 seed, bool last);

/*! @} */
