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
/*! \file frb_log.h
 * @brief define operation of ftl root block
 *
 * \addtogroup frb
 *
 * @{
 * define FTL root block interface function and initialization
 */
#pragma once

#include "bf_mgr.h"
#include "queue.h"
#include "ncl_exports.h"

#define NR_FRB_LOG_DU_CNT	DU_CNT_PER_PAGE
#define NR_FRB_TYPE		8

/*!
 * @brief frb type list
 */
#define FRB_TYPE_HEADER		7
#define FRB_TYPE_DEFECT		6
#define FRB_TYPE_REMAP		5

/*!
 * @brief frb pend list define
 */
#define FRB_PEND_HEADER		(1 << FRB_TYPE_HEADER)
#define FRB_PEND_DEFECT		(1 << FRB_TYPE_DEFECT)

#define FRB_PEND_ALL		0xE0


/*!
 * @brief frb log type flags
 */
#define FRB_LOG_IDX_F_IO_BUF	0x01		///< FRB buffer is IO buffer, don't need to allocate buffer when flush

/*!
 * @brief Initialize FRB log
 *
 * This function initialize FRB log, input value should be gotten from MR.
 * Flush id starts from value 1
 *
 * @param blk_id0		FRB log block 0 from SRB
 * @param blk_id1		FRB log block 1 from SRB //hs remove
 *
 * @return			not used
 */
void frb_log_init(u32 blk_id0, u32 spb_id);

/*!
 * @brief reset frb header table grown defect info
 *
 * @return		not used
 */
void reset_grwn_defect(void);

/*!
 * @brief get grown defect spb id
 *
 * @param ofst offset of the grown defect table
 *
 * @return		spb id
 */
u32 get_grwn_defect_info(u32 ofst);

/*!
 * @brief FRB log start function
 *
 * This function initialize frb_log_io resource and rebuild FRB log info
 * Input ptr will be forward to reconstruction function to update
 * If virgin boot then skip reconstruction
 *
 * @return		FRB rebuild finish return true
 */
bool frb_log_start(void);

/*!
 * @brief set frb log flush callback function
 *
 * we only have one callback and context resource, use it carefully
 * now, only uart command and update log spb share this resource
 *
 * @param ctx	caller context
 * @param cmpl	competion callback
 *
 * @return	not used
 */
void frb_log_set_cb(void *ctx, completion_t cmpl);

/*!
 * @brief load frb log from ftl root block
 *
 * @param type		frb log type
 * @param buf		destination buffer, could be NULL
 *
 * @note if buf == null, the data will be loaded into default buffer
 *
 * @return		not used
 */
void frb_log_type_load(u32 type, void *buf);

/*!
 * @brief flush frb log to ftl root block
 *
 * @return		not used
 */
void frb_log_type_update(u32 type);

/*!
 * @brief initial frb pg_idx
 *
 * @param type		type to initial
 * @param size		data size in frb log page
 * @param mem 		memory location for type
 * @param flags 	FRB_LOG_IDX_F_xxx
 *
 * @return		not used
 */
void frb_log_type_init(u32 type, u32 size, void *mem, u8 flags);

/*!
 * @brief initial frb resource
 *
 * @return		not used
 */
void frb_log_res_init(void);

/*!
 * @brief insert grown defect to frb header
 *
 * @return		not used
 */
void frb_log_update_grwn_def(u8 type, pda_t pda);

/*!
 * @brief if we don't have spare count in frb, set it
 *
 * @param spare		current spare count, unit is block
 *
 * @return		not used
 */
void frb_log_set_spare_cnt(u32 spare);

/*!
 * @brief interface to get spare count (max spare count)
 *
 * @return		return recorded spare count
 */
u32 frb_log_get_spare_cnt(void);

/*!
 * @brief interface to resume defect mgr frb log after pmu enter
 *
 * @return		return recorded spare count
 */
void defect_mgr_resume(void);
void frb_log_set_qbt_info(u32 TAG, u16 blk_id, u32 blk_sn);
u32 frb_log_get_ftl_tag(void);
u16 frb_log_get_qbt_blk(void);
u32 frb_log_get_qbt_sn(void);
extern bool QBT_TAG;
extern u16  QBT_BLK_IDX;
//void frb_log_set_qbt_grp(qbt_grp_t grp1_blk, qbt_grp_t grp2_blk, qbt_grp_t grp3_blk);
//qbt_grp_t frb_log_get_qbt_grp(u8 grp);

/*! @} */
