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
 * \file stat.h
 * \brief This file contains definitions of statistics information which was stored in nand
 * \addtogroup statistics
 * @{
 *
 */

/*!
 * @brief header definition of data structure stored in nand
 */
typedef struct _stat_head_t {
	u16 version;		///< version
	u16 field_size;		///< useful field size, include head
	u32 signature;		///< signature of data structrue
} stat_head_t;

/*!
 * @brief initialize head
 *
 * @param stat		pointer of head
 * @param ver		current version
 * @param fsize		field size
 * @param sig		signature
 *
 * @return		none
 */
void stat_init(stat_head_t *stat, u16 ver, u16 fsize, u32 sig);

/*!
 * @brief restore data structure from nand,
 *
 * if signature was mismatch, if will set all to zero
 * if version was mismatch, if will help to upgrade
 *
 * @param dst		destination pointer
 * @param src		source pointer, should be read from nand
 * @param ttl_size	total data structure size including head
 *
 * @return		none
 */
void stat_restore(stat_head_t *dst, stat_head_t *src, u32 ttl_size);

/*!
 * @brief to check if read data structure is valid or not
 *
 * @param src		source pointer, should be read from nand
 * @param ver		current version
 * @param fsize		current field size, including head
 * @param sig		current signature
 *
 * @return		return true if valid
 */
bool stat_match(stat_head_t *src, u16 ver, u16 fsize, u32 sig);

/*! @} */
