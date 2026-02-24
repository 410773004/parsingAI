//-----------------------------------------------------------------------------
//                 Copyright(c) 2016-2018 Innogrit Corporation
//                             All Rights reserved.
//
// The confidential and proprietary information contained in this file may
// only be used by a person authorized under and to the extent permitted
// by a subsisting licensing agreement from Innogrit Corporation.
// Dissemination of this information or reproduction of this material
// is strictly forbidden unless prior written permission is obtained
// from Innogrit Corporation.
//-----------------------------------------------------------------------------

/*! \file flog.h
 * @brief memory log implementation for observation
 *
 * \addtogroup utils
 * \defgroup flog
 * \ingroup utils
 * @{
 */
#pragma once

/*!
 * @brief memory log entry, four dword
 */
typedef struct _flog_ent_t {
	u32 d[4];
} flog_ent_t;

/*!
 * @brief memory log definition
 */
typedef struct _flog_t {
	u16 idx;		///< current index to insert
	u16 total;		///< total entry count in memory log
	flog_ent_t ent[1];	///< memory log buffer
} flog_t;

/*!
 * @brief initialization memory log
 *
 * @param size		entry count in memory log
 *
 * @return		return memory log object
 */
flog_t *flog_init(u32 size);

/*!
 * @brief add new entry into memory log
 *
 * @param flog		memory log object
 * @param a		1st dword
 * @param b		2nd dword
 * @param c		3rd dword
 * @param d		4th dword
 *
 * @return		not used
 */
void flog_add(flog_t *flog, u32 a, u32 b, u32 c, u32 d);

/*! @} */
