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
/*! \file defect_mgr.h
 * @brief define defect table and how to load and usage
 *
 * \addtogroup defect_mgr
 *
 * @{
 * initialize defect table from MR or scan by FTL. Provide the interface for FTL
 * to get defect bitmap and count for a specific SPB.
 */
#pragma once

#ifdef SKIP_MODE
share_data_zi u8* gl_pt_defect_tbl;
#endif

/*!
 * @brief initialize FTL defect map
 *
 * If ftl root block was existed, we could just load defect from it
 *
 * @param frb_valid	true if ftl root block was valid
 *
 * @return		none
 *
 */
void ftl_init_defect(bool *frb_valid);

/*!
 * @brief initialize remap idx
 *
 * @return		none
 *
 */
void remap_idx_init(void);

/*!
 * @brief check defect version loaded from frb
 *
 * version-wise backward/forward compatibility handle
 *
 * @param		remap was enabled in this boot
 *
 * @return		false for force rebuild defect table
 *
 */
u32 defect_version_check(bool remap);
u8* get_defect_map(void);


/*!
 * @brief interface to get defect map for SPB
 *
 * @param spb_id	spb id
 * @param ftl_df	defect structure for a SPB
 *
 * @return		none
 */
void spb_get_defect_map(spb_id_t spb_id, ftl_defect_t *ftl_df);

/*!
 * @brief interface to get defect count
 *
 * @param spb_id	spb id
 *
 * @return		return defect count of SPB
 */
u32 spb_get_defect_cnt(spb_id_t spb_id);

#ifdef SKIP_MODE //Alan Huang
/*!
 * @brief interface to check defect count for mark bad SPB
 *
 * @param spb_id	spb id
 *
 * @return		none
 */
void spb_check_defect_cnt(u16 spb_id);
#endif

/*!
 * @brief interface to set defect for grown defect
 *
 * @param spb_id	spb id
 * @param blk		defect block in spb
 *
 * @return		defect bloc increased count
 */
u32 spb_set_defect_blk(spb_id_t spb_id, u32 blk);

/*!
 * @brief insert grown defect error information
 *
 * @param type		type of grown defect
 * @param pda		error pda
 *
 * @return		none
 */
void ins_grwn_err_info(u8 type, pda_t pda);

/*!
 * @brief backup physical block table to FRB
 *
 * @return		not used
 */
void update_pblk_tbl(void);

struct _phy_blk_t;
/*!
 * @brief get physical block table pointer in frb
 *
 * @return		pointer of physical block table in frb
 */
struct _phy_blk_t *get_pblk_tbl(void);
/*! @} */
