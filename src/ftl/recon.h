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
#if 0
typedef void (*scan_handler_t)(lda_t *lda_list, pda_t pda, u32 page_cnt);

/*!
 * @brief force scan a whole SPB
 *
 * @param can	scan candidate
 * @param nsid	ftl namespace id
 * @param slc	true if can is slc spb
 *
 * @return	return true if can spb was not programmed
 */
bool recon_force_scan_spb(spb_id_t can, u32 nsid, bool slc);

/*!
 * @brief check if spb is closed
 *
 * @param spb	dirty spb
 * @param host	true for host spb
 *
 * @return	true if spb is closed
 */
bool recon_chk_spb_closed(spb_id_t spb, bool host);

/*!
 * @brief recon l2p table by load spb p2l
 *
 * @param spb	dirty spb
 *
 * @return	not used
 */
void recon_spb_with_p2l(spb_id_t spb);

/*!
 * @brief recon l2p table by scan spb meta
 *
 * @param spb		dirty spb
 * @param ptr		write ptr to start scan
 * @param handler	scan result handler
 * @param host		if host spb
 *
 * @return		return true if can spb was not programmed
 */
bool recon_spb_with_meta(spb_id_t spb, u32 ptr, scan_handler_t handler, bool host);

/*!
 * @brief recon trim info du
 *
 * @param pda		pda of the trim du
 *
 * @return		not used
 */
u32 recon_trim_info(u32 pda);

/*!
 * @brief update trim based on trim info
 *
 * @param ftl_trim	pointer to trim info
 *
 * @return		not used
 */
void recon_trim_handle(ftl_trim_t *ftl_trim);

/*!
 * @brief get reconstruction fence
 *
 * @param ffence	namespace's fence
 * @param type		FTL_CORE_NRM or FTL_CORE_GC
 *
 * @return	recon fence(spb/sn/ptr)
 */
spb_fence_t *get_recon_fence(ftl_fence_t *ffence, u32 type);

/*!
 * @brief restore ns fence with found dirty spb
 *
 * @param fence_spb	latest fence spb
 * @param nsid		namespace id
 *
 * @return	not used
 */
void recon_restore_fence(spb_id_t *fence_spb, u32 nsid);

/*!
 * @brief initialize reconstruction resource
 *
 * @return		not used
 */
void recon_res_get(void);

/*!
 * @brief delete reconstruction resource
 *
 * @return		not used
 */
void recon_res_put(void);

/*!
 * @brief allocate index meta and p2l ddr dtag
 *
 * @return		not used
 */
void recon_pre_init(void);

/*!
 * @brief check if du is in raid lun
 *
 * @param idx		du index in interleave
 *
 * @return		true if du in raid lun
 */
bool is_raid_lun(u32 idx);
#endif