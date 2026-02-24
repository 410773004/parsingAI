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
/*! \file ucache.h
 * @brief ucache support
 *
 * \addtogroup dispatcher
 * \defgroup rainier
 * \ingroup dispatcher
 * @{
 */

#pragma once
#include "types.h"
#include "bf_mgr.h"
#define WUNC_MAX_CACHE  (192)
typedef struct wunc_t{
    u16 ceidxs[WUNC_MAX_CACHE];
    lda_t startlda0;
    lda_t startlda1;
    lda_t endlda0;
    lda_t endlda1;
    u32 cross_cnt;
}wunc_t;
extern bool flush_que_chk(void);

/*!
 * @brief check if cache clean
 *
 * @return		not used
 */
extern bool ucache_clean(void);

/*!
 * @brief l2p pda update done handle
 *
 * @param did		dtag to put
 *
 * @return		not used
 */
extern void ucache_pda_updt_done(u32);
extern void wunc_handle_done(u32 did);

/*!
 * @brief l2p pda update abort handle
 *
 * @param did		dtag aborted
 *
 * @return		not used
 */
extern void ucache_pda_updt_abort(u32);

/*!
 * @brief partial data input to cache
 *
 * @param lda		lda
 * @param dtag		dtag
 * @param btag		btag
 * @param ofst		lba offset of the lda
 * @param nlba		number of lba
 *
 * @return		not used
 */
extern void ucache_par_data_in(lda_t, dtag_t, u16, int, int);
//extern void par_data_in_handle(ce_t* ce);

/*!
 * @brief normal/par data input to cache
 *
 * @param lda		lda
 * @param dtag		dtag
 * @param btag		btag
 * @param par		partail data info
 * @param count		number of lda
 *
 * @return		not used
 */
extern void ucache_nrm_par_data_in(lda_t *, dtag_t *, u16 *, r_par_t *, int);

/*!
 * @brief fw read data input to cache
 *
 * @param pls		layload list to handle
 * @param count		number of payload
 *
 * @return		not used
 */
extern void ucache_fr_data_in(bm_pl_t *pls, int count,bool error);

/*!
 * @brief unmap data search handle
 *
 * @param ofst		offset of the search list
 * @param pop		pop dtag from list
 *
 * @return		not used
 */
extern void ucache_unmap_data_in(int ofst, bool pop, bool error);

/*!
 * @brief read data out from cache
 *
 * @param btag		btag of the read command
 * @param lda		lda to be read
 * @param count		number of lda to read
 *
 * @return		not used
 */
extern void ucache_read_data_out(u32 btag, lda_t lda, int count, u32 offset);
//extern void ucache_read_data_out_cross(u32, lda_t, int,u32);//joe add NS 20200813

/*!
 * @brief read one data out from cache
 *
 * @param btag		btag of the read command
 * @param lda		lda to be read
 *
 * @return		true if read data executed
 */
extern void ucache_single_read_data_out(u32 btag, lda_t lda, int count, u32 offset);

/*!
 * @brief flush cache
 *
 * @param fctx		flush context
 *
 * @return		not used
 */
extern void ucache_flush(ftl_flush_data_t *fctx);

extern void ucache_range_trim_done(u32 range);
/*!
 * @brief trim handle interface
 *
 * @param req		pointer to trim request
 *
 * @return		not used
 */
extern void ucache_trim(req_t *req);

/*!
 * @brief trim info handle done
 *
 * @param req		pointer to trim request
 *
 * @return		not used
 */
extern void rdisk_trim_info_done(req_t *req);

/*!
 * @brief cache initialization
 *
 * @param nlba		disk size in lba
 *
 * @return		not used
 */
extern int ucache_init(u64 nlba);

/*!
 * @brief cache resume from PMU
 *
 * @return		not used
 */
extern void ucache_resume(void);
extern void ucache_reset_resume(void);
/*!
 * @brief cache suspend for PMU
 *
 * @return		not used
 */
extern void ucache_suspend(void);

extern void ucache_dump(void);

extern bool ucache_reader_check(void);

extern void ucache_free_unused_dtag(void);

extern void pln_flush(void);

/*! @} */
