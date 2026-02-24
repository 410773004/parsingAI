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

//=============================================================================
//
/*! \file spb_info.h
 * @brief spb info is a table to store erase count, and pb type
 *
 * \addtogroup spb_info
 * @{
 *
 * spb info is a global table and it is stored in SPB log, the reconstruction
 * method of spb log is exported from FTL, outside module can reconstructed it
 * to get spb info table without FTL initialization.
 */
#pragma once
#include "types.h"
#include "ftl.h"

#define SPB_INFO_F_SLC	0x01	  ///< spb info flag to indicate this spb is SLC
#define SPB_INFO_F_NATIVE	0x02  ///< spb info flag to indicate this spb is native
#define SPB_INFO_F_MIX	0x04	  ///< spb info flag to indicate this spb can be slc or native
#define SPB_INFO_F_OVER_TEMP 0x08 ///< spb info flag to indicate this spb over temperature need GC after temperature normal

#define POHTAG             0x504f48     //P=0x50, O=0x4F, H=0x48
/*! @brief entry of spb info table */
typedef struct _spb_info_t {
	u32 erase_cnt;	///< erase count
	u32 poh;
	u16 block;     // next block idx
	u8  flags;		///< SPB_INFO_F_XXX
	u8  pool_id;		///< pool id
} spb_info_t;
extern fast_data spb_info_t *spb_info_tbl;	///< spb info table

/*!
 * @brief initialize spb info memory, and reconstructed it from spb log
 *
 * @param spb_id	spb id of spb log
 *
 * @return		return virgin if no spb log spb or reconstructed fail
 */
ftl_err_t spb_info_init(ftl_initial_mode_t mode);

/*!
 * @brief reset all flag in spb info to re-dispatch SPB
 *
 * @return		return FTL_ERR_VIRGIN
 */
ftl_err_t spb_info_flag_reset(void);

/*!
 * @brief get spb info of specfic spb
 *
 * @param spb_id	spb id
 *
 * @return		spb info of spb
 */
static inline spb_info_t *spb_info_get(u32 spb_id)
{
	extern spb_info_t *spb_info_tbl;

	return &spb_info_tbl[spb_id];
}

/*!
 * @brief flush spb info table to spb log
 *
 * @param spb_id	which spb to be flushed
 *
 * @return		none
 */
void spb_info_flush(spb_id_t spb_id);

/*! @} */


