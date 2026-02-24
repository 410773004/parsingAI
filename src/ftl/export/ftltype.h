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
/*! \file ftltype.h
 * @brief export ftl interface, type definition used in FTL
 *
 * \addtogroup ftl_export
 * @{
 */
#pragma once

#include "types.h"

typedef void (*completion_t)(void *_ctx);	///< completion callback type without return value
typedef int (*callback_t)(void *_ctx);		///< completion callback type with return value

typedef u16 spb_id_t;				///< spb_id type
#define INV_SPB_ID	0xFFFF			///< invalid SPB id
#define ERR_SPB_ID	0xFFFE			///< error SPB id

typedef u8  pb_id_t;				///< pb id type
#define INV_PB_ID  0xFF				///< invalid pb id

typedef u32 sn_t;				///< sn type
#define INV_SN      0xFFFFFFFF			///< invalid sn

/*!
 * @brief definition of spb including type and SN, used between CPU
 */
typedef union _spb_ent_t {
	struct {
		u64 spb_id : 16;	///< spb id
		u64 slc    : 1;		///< if slc
		u64 erFal  : 1;		///< spb erase failed found, ISU, SPBErFalHdl
		u64 rsvd   : 13;
		u64 ps_abort: 1;
		u64 sn     : 32;	///< serial number
	} b;
	u64 all;
} spb_ent_t;
BUILD_BUG_ON(sizeof(spb_ent_t) != sizeof(u64));

/*! @} */

